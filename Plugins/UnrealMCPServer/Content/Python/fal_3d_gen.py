# Copyright StraySpark Studio 2026. All Rights Reserved.

"""
fal.ai 3D Model Generation for Unreal MCP Server
Supports: text-to-3d and image-to-3d via multiple fal.ai models.

Text-to-3D models:
  - meshy-v6: Meshy 6 (high quality, textured, supports PBR/rigging/animation)
  - meshy-v6-preview: Meshy 6 Preview (faster, untextured)
  - hunyuan-pro: Hunyuan 3D v3.1 Pro (high fidelity, up to 1.5M faces)
  - hunyuan-rapid: Hunyuan 3D v3.1 Rapid (faster variant)
  - hunyuan-v3: Hunyuan3D v3 text-to-3d

Image-to-3D models:
  - trellis-2: Trellis 2 (best overall quality, native 3D generation)
  - meshy-v6-img: Meshy 6 image-to-3d (supports PBR/rigging/animation)
  - hunyuan-v3-img: Hunyuan3D v3 image-to-3d
  - hunyuan-pro-img: Hunyuan 3D v3.1 Pro image-to-3d
  - rodin-v2: Hyper3D Rodin v2 (production-ready assets)
  - tripo-v2: Tripo3D v2.5 image-to-3d

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

# ─── Text-to-3D Models ───
TEXT_TO_3D_MODELS = {
    "meshy-v6":         "fal-ai/meshy/v6/text-to-3d",
    "meshy-v6-preview": "fal-ai/meshy/v6-preview/text-to-3d",
    "hunyuan-pro":      "fal-ai/hunyuan-3d/v3.1/pro/text-to-3d",
    "hunyuan-rapid":    "fal-ai/hunyuan-3d/v3.1/rapid/text-to-3d",
    "hunyuan-v3":       "fal-ai/hunyuan3d-v3/text-to-3d",
}

# ─── Image-to-3D Models ───
IMAGE_TO_3D_MODELS = {
    "trellis-2":        "fal-ai/trellis-2",
    "meshy-v6-img":     "fal-ai/meshy/v6/image-to-3d",
    "hunyuan-v3-img":   "fal-ai/hunyuan3d-v3/image-to-3d",
    "hunyuan-pro-img":  "fal-ai/hunyuan-3d/v3.1/pro/image-to-3d",
    "rodin-v2":         "fal-ai/hyper3d/rodin-v2",
    "tripo-v2":         "fal-ai/tripo3d/tripo-v2.5/image-to-3d",
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
        with urllib.request.urlopen(req, timeout=300) as resp:
            return json.loads(resp.read().decode("utf-8"))
    except urllib.error.HTTPError as e:
        error_body = e.read().decode("utf-8", errors="replace")
        raise RuntimeError(f"fal.ai API error ({e.code}): {error_body}")
    except urllib.error.URLError as e:
        raise RuntimeError(f"Network error: {e.reason}")


def _poll_queue(model_id, request_id, api_key, max_wait=600):
    """Poll fal.ai queue until request completes. 3D gen can take 5-10 min."""
    status_url = f"{FAL_QUEUE_URL}/{model_id}/requests/{request_id}/status"
    result_url = f"{FAL_QUEUE_URL}/{model_id}/requests/{request_id}"

    start = time.time()
    while time.time() - start < max_wait:
        status = _make_request(status_url, api_key=api_key, method="GET")
        s = status.get("status", "")
        if s == "COMPLETED":
            return _make_request(result_url, api_key=api_key, method="GET")
        elif s in ("FAILED", "CANCELLED"):
            raise RuntimeError(f"fal.ai request {s}: {json.dumps(status)[:500]}")
        time.sleep(5)  # 3D gen is slower, poll every 5s

    raise RuntimeError(f"fal.ai request timed out after {max_wait}s")


def _submit_and_get_result(model_id, payload, api_key):
    """Submit to fal.ai - tries sync first, falls back to queue."""
    sync_url = f"{FAL_SYNC_URL}/{model_id}"
    try:
        result = _make_request(sync_url, data=payload, api_key=api_key)
        return result
    except RuntimeError as e:
        err_str = str(e)
        if "405" in err_str or "422" in err_str or "404" in err_str:
            pass
        else:
            raise

    # Fall back to queue endpoint (most 3D models require this)
    queue_url = f"{FAL_QUEUE_URL}/{model_id}"
    response = _make_request(queue_url, data=payload, api_key=api_key)

    if "model_glb" in response:
        return response
    elif "request_id" in response:
        return _poll_queue(model_id, response["request_id"], api_key)
    else:
        raise RuntimeError(f"Unexpected fal.ai response: {json.dumps(response)[:500]}")


def _download_file(url, output_path):
    """Download a file from URL to disk."""
    req = urllib.request.Request(url)
    with urllib.request.urlopen(req, timeout=120) as resp:
        with open(output_path, "wb") as f:
            f.write(resp.read())


def _extract_glb_url(result):
    """Extract GLB download URL from various response formats."""
    # Direct model_glb field (most models)
    if "model_glb" in result and isinstance(result["model_glb"], dict):
        url = result["model_glb"].get("url")
        if url:
            return url

    # model_urls.glb (Meshy format)
    model_urls = result.get("model_urls", {})
    if isinstance(model_urls, dict):
        glb = model_urls.get("glb")
        if isinstance(glb, dict):
            url = glb.get("url")
            if url:
                return url

    # Direct glb_url field (some models)
    if "glb_url" in result:
        return result["glb_url"]

    return None


def _extract_thumbnail_url(result):
    """Extract thumbnail preview URL if available."""
    if "thumbnail" in result and isinstance(result["thumbnail"], dict):
        return result["thumbnail"].get("url")
    # Some models return video preview
    if "preview_video" in result and isinstance(result["preview_video"], dict):
        return result["preview_video"].get("url")
    return None


def text_to_3d(prompt, api_key, output_dir, model="meshy-v6",
               model_name=None, art_style="realistic", topology="triangle",
               target_polycount=30000, enable_pbr=False):
    """
    Generate a 3D model from text using fal.ai.

    Returns dict: {"success": bool, "glb_path": str, "thumbnail_path": str, "error": str}
    """
    try:
        model_id = TEXT_TO_3D_MODELS.get(model)
        if not model_id:
            return {"success": False, "error": f"Unknown text-to-3d model: {model}. Use: {list(TEXT_TO_3D_MODELS.keys())}"}

        os.makedirs(output_dir, exist_ok=True)

        if not model_name:
            prompt_hash = hashlib.md5(prompt.encode()).hexdigest()[:8]
            model_name = f"SM_AI_{prompt_hash}"

        glb_path = os.path.join(output_dir, f"{model_name}.glb")

        # Build payload based on model type
        payload = {"prompt": prompt}

        if model.startswith("meshy"):
            payload["mode"] = "full"
            payload["art_style"] = art_style
            payload["topology"] = topology
            payload["target_polycount"] = target_polycount
            payload["should_remesh"] = True
            if enable_pbr:
                payload["enable_pbr"] = True
        elif model.startswith("hunyuan"):
            payload["generate_type"] = "Normal"
            if enable_pbr:
                payload["enable_pbr"] = True
            if target_polycount > 0:
                clamped = max(40000, min(1500000, target_polycount))
                payload["face_count"] = clamped

        result = _submit_and_get_result(model_id, payload, api_key)

        glb_url = _extract_glb_url(result)
        if not glb_url:
            return {"success": False, "error": f"No GLB URL in response. Keys: {list(result.keys())}"}

        _download_file(glb_url, glb_path)

        output = {"success": True, "glb_path": glb_path, "model_name": model_name}

        # Download thumbnail if available
        thumb_url = _extract_thumbnail_url(result)
        if thumb_url:
            thumb_path = os.path.join(output_dir, f"{model_name}_thumb.png")
            try:
                _download_file(thumb_url, thumb_path)
                output["thumbnail_path"] = thumb_path
            except Exception:
                pass  # Thumbnail is optional

        # Include texture URLs if available (Meshy PBR)
        texture_urls = result.get("texture_urls")
        if texture_urls and isinstance(texture_urls, list) and len(texture_urls) > 0:
            tex = texture_urls[0]
            pbr_paths = {}
            for tex_type in ["base_color", "metallic", "normal", "roughness"]:
                if tex_type in tex and isinstance(tex[tex_type], dict):
                    tex_url = tex[tex_type].get("url")
                    if tex_url:
                        tex_path = os.path.join(output_dir, f"{model_name}_{tex_type}.png")
                        try:
                            _download_file(tex_url, tex_path)
                            pbr_paths[tex_type] = tex_path
                        except Exception:
                            pass
            if pbr_paths:
                output["pbr_textures"] = pbr_paths

        return output

    except Exception as e:
        return {"success": False, "error": str(e)}


def image_to_3d(image_url, api_key, output_dir, model="trellis-2",
                model_name=None, topology="triangle", target_polycount=30000,
                enable_pbr=False, texture_size=2048):
    """
    Generate a 3D model from an image using fal.ai.

    Args:
        image_url: URL of source image (publicly accessible) or local file path
        model: Model key from IMAGE_TO_3D_MODELS

    Returns dict: {"success": bool, "glb_path": str, "thumbnail_path": str, "error": str}
    """
    try:
        model_id = IMAGE_TO_3D_MODELS.get(model)
        if not model_id:
            return {"success": False, "error": f"Unknown image-to-3d model: {model}. Use: {list(IMAGE_TO_3D_MODELS.keys())}"}

        os.makedirs(output_dir, exist_ok=True)

        if not model_name:
            url_hash = hashlib.md5(image_url.encode()).hexdigest()[:8]
            model_name = f"SM_AI_{url_hash}"

        glb_path = os.path.join(output_dir, f"{model_name}.glb")

        # Build payload
        payload = {"image_url": image_url}

        if model.startswith("trellis"):
            payload["resolution"] = 1024
            payload["texture_size"] = texture_size
            payload["remesh"] = True
            if target_polycount > 0:
                clamped = max(5000, min(2000000, target_polycount))
                payload["decimation_target"] = clamped
        elif model.startswith("meshy"):
            payload["topology"] = topology
            payload["target_polycount"] = target_polycount
            payload["should_remesh"] = True
            payload["should_texture"] = True
            if enable_pbr:
                payload["enable_pbr"] = True
        elif model.startswith("hunyuan"):
            payload["generate_type"] = "Normal"
            if enable_pbr:
                payload["enable_pbr"] = True
        elif model == "rodin-v2":
            pass  # rodin uses defaults
        elif model == "tripo-v2":
            pass  # tripo uses defaults

        result = _submit_and_get_result(model_id, payload, api_key)

        glb_url = _extract_glb_url(result)
        if not glb_url:
            return {"success": False, "error": f"No GLB URL in response. Keys: {list(result.keys())}"}

        _download_file(glb_url, glb_path)

        output = {"success": True, "glb_path": glb_path, "model_name": model_name}

        # Thumbnail
        thumb_url = _extract_thumbnail_url(result)
        if thumb_url:
            thumb_path = os.path.join(output_dir, f"{model_name}_thumb.png")
            try:
                _download_file(thumb_url, thumb_path)
                output["thumbnail_path"] = thumb_path
            except Exception:
                pass

        # PBR textures (Meshy)
        texture_urls = result.get("texture_urls")
        if texture_urls and isinstance(texture_urls, list) and len(texture_urls) > 0:
            tex = texture_urls[0]
            pbr_paths = {}
            for tex_type in ["base_color", "metallic", "normal", "roughness"]:
                if tex_type in tex and isinstance(tex[tex_type], dict):
                    tex_url = tex[tex_type].get("url")
                    if tex_url:
                        tex_path = os.path.join(output_dir, f"{model_name}_{tex_type}.png")
                        try:
                            _download_file(tex_url, tex_path)
                            pbr_paths[tex_type] = tex_path
                        except Exception:
                            pass
            if pbr_paths:
                output["pbr_textures"] = pbr_paths

        return output

    except Exception as e:
        return {"success": False, "error": str(e)}
