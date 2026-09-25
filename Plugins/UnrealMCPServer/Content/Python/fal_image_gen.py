# Copyright StraySpark Studio 2026. All Rights Reserved.

"""
fal.ai Image Generation for Unreal MCP Server
Supports: nano-banana, nano-banana-2, flux-v2 models
Background removal: fal-ai/birefnet/v2

Called from UE Python via runpy.run_path() - functions are imported into caller's namespace.
"""

import json
import os
import time
import urllib.request
import urllib.error
import hashlib

# fal.ai endpoint base URLs
FAL_SYNC_URL = "https://fal.run"
FAL_QUEUE_URL = "https://queue.fal.run"

MODEL_MAP = {
    "nano-banana": "fal-ai/nano-banana",
    "nano-banana-2": "fal-ai/nano-banana-2",
    "flux-2-flash": "fal-ai/flux-2/flash",
    "flux-dev": "fal-ai/flux/dev",
    "flux-pro": "fal-ai/flux-pro/v1.1",
}

BIREFNET_MODEL = "fal-ai/birefnet/v2"

STYLE_PRESETS = {
    "ui_icon": "flat design, clean edges, game UI icon, transparent background, PNG, centered composition",
    "ui_background": "seamless texture, game UI background, subtle pattern, dark theme, tileable",
    "ui_button": "game button texture, beveled edges, clean design, rectangular, game UI element",
    "ui_frame": "game UI frame border, ornate edges, transparent center, decorative border element",
    "ui_portrait": "character portrait, game art style, detailed, square composition",
    "custom": "",
}

# Map our size names to fal.ai aspect_ratio values
SIZE_TO_ASPECT = {
    "square": "1:1",
    "square_hd": "1:1",
    "landscape_4_3": "4:3",
    "landscape_16_9": "16:9",
    "portrait_4_3": "3:4",
    "portrait_16_9": "9:16",
}


def _make_request(url, data=None, api_key=None, method="POST"):
    """Make an HTTP request to fal.ai API."""
    headers = {
        "Content-Type": "application/json",
        "Accept": "application/json",
    }
    if api_key:
        headers["Authorization"] = "Key " + api_key

    body = json.dumps(data).encode("utf-8") if data else None
    req = urllib.request.Request(url, data=body, headers=headers, method=method)

    try:
        with urllib.request.urlopen(req, timeout=180) as resp:
            return json.loads(resp.read().decode("utf-8"))
    except urllib.error.HTTPError as e:
        error_body = e.read().decode("utf-8", errors="replace")
        raise RuntimeError(f"fal.ai API error ({e.code}): {error_body}")
    except urllib.error.URLError as e:
        raise RuntimeError(f"Network error: {e.reason}")


def _poll_queue(model_id, request_id, api_key, max_wait=180):
    """Poll fal.ai queue until request completes."""
    status_url = f"{FAL_QUEUE_URL}/{model_id}/requests/{request_id}/status"
    result_url = f"{FAL_QUEUE_URL}/{model_id}/requests/{request_id}"

    start = time.time()
    while time.time() - start < max_wait:
        status = _make_request(status_url, api_key=api_key, method="GET")
        s = status.get("status", "")
        if s == "COMPLETED":
            return _make_request(result_url, api_key=api_key, method="GET")
        elif s in ("FAILED", "CANCELLED"):
            raise RuntimeError(f"fal.ai request {s}: {status}")
        time.sleep(2)

    raise RuntimeError(f"fal.ai request timed out after {max_wait}s")


def _submit_and_get_result(model_id, payload, api_key):
    """Submit to fal.ai - tries sync first, falls back to queue."""
    # Try synchronous endpoint first
    sync_url = f"{FAL_SYNC_URL}/{model_id}"
    try:
        result = _make_request(sync_url, data=payload, api_key=api_key)
        return result
    except RuntimeError as e:
        err_str = str(e)
        # If sync not supported (405, 422, etc), try queue
        if "405" in err_str or "422" in err_str or "404" in err_str:
            pass
        else:
            raise

    # Fall back to queue endpoint
    queue_url = f"{FAL_QUEUE_URL}/{model_id}"
    response = _make_request(queue_url, data=payload, api_key=api_key)

    if "images" in response or "image" in response:
        return response
    elif "request_id" in response:
        return _poll_queue(model_id, response["request_id"], api_key)
    else:
        raise RuntimeError(f"Unexpected fal.ai response: {json.dumps(response)[:500]}")


def _download_image(url, output_path):
    """Download an image from URL to disk."""
    req = urllib.request.Request(url)
    with urllib.request.urlopen(req, timeout=60) as resp:
        with open(output_path, "wb") as f:
            f.write(resp.read())


def generate(prompt, api_key, output_dir, model="flux-v2", style_preset="custom",
             image_size="square_hd", image_name=None, remove_background=False):
    """
    Generate an image using fal.ai and save to disk.

    Returns dict: {"success": bool, "path": str, "error": str}
    """
    try:
        model_id = MODEL_MAP.get(model)
        if not model_id:
            return {"success": False, "error": f"Unknown model: {model}. Use: {list(MODEL_MAP.keys())}"}

        # Apply style preset
        style_suffix = STYLE_PRESETS.get(style_preset, "")
        full_prompt = f"{prompt}, {style_suffix}" if style_suffix else prompt

        os.makedirs(output_dir, exist_ok=True)

        if not image_name:
            prompt_hash = hashlib.md5(full_prompt.encode()).hexdigest()[:8]
            image_name = f"T_UI_Gen_{prompt_hash}"

        output_path = os.path.join(output_dir, f"{image_name}.png")

        # Build payload - use aspect_ratio for nano-banana models, image_size for flux
        aspect_ratio = SIZE_TO_ASPECT.get(image_size, "1:1")
        payload = {
            "prompt": full_prompt,
            "aspect_ratio": aspect_ratio,
            "num_images": 1,
            "output_format": "png",
            "sync_mode": True,
        }

        result = _submit_and_get_result(model_id, payload, api_key)

        # Extract image URL from response
        images = result.get("images", [])
        if not images:
            return {"success": False, "error": f"No images in response. Keys: {list(result.keys())}"}

        image_url = images[0].get("url")
        if not image_url:
            return {"success": False, "error": "No image URL in response"}

        # Optional background removal
        if remove_background:
            bg_result = _remove_bg_internal(image_url, api_key)
            bg_url = bg_result.get("url")
            if bg_url:
                image_url = bg_url

        _download_image(image_url, output_path)

        return {"success": True, "path": output_path, "image_name": image_name}

    except Exception as e:
        return {"success": False, "error": str(e)}


def _remove_bg_internal(image_url, api_key):
    """Internal: call birefnet to remove background, return image dict with url."""
    payload = {
        "image_url": image_url,
        "output_format": "png",
        "refine_foreground": True,
        "operating_resolution": "1024x1024",
    }

    result = _submit_and_get_result(BIREFNET_MODEL, payload, api_key)

    # birefnet returns {"image": {"url": ...}} not {"images": [...]}
    if "image" in result and isinstance(result["image"], dict):
        return result["image"]
    return {}


def remove_bg(image_url, api_key, output_dir, image_name=None):
    """
    Remove background from an image using fal-ai/birefnet/v2.

    Returns dict: {"success": bool, "path": str, "error": str}
    """
    try:
        os.makedirs(output_dir, exist_ok=True)

        if not image_name:
            url_hash = hashlib.md5(image_url.encode()).hexdigest()[:8]
            image_name = f"T_UI_NoBG_{url_hash}"

        output_path = os.path.join(output_dir, f"{image_name}.png")

        result = _remove_bg_internal(image_url, api_key)
        result_url = result.get("url")
        if not result_url:
            return {"success": False, "error": "No image URL in birefnet response"}

        _download_image(result_url, output_path)

        return {"success": True, "path": output_path, "image_name": image_name}

    except Exception as e:
        return {"success": False, "error": str(e)}
