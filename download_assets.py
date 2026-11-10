import json
import os
import subprocess
import time
import urllib.parse

with open('asset_urls.json') as f:
    raw_urls = json.load(f)

# Valid asset extensions
valid_exts = ('.gif', '.jpg', '.jpeg', '.png', '.swf', '.css', '.ico')

asset_urls = []
for u in raw_urls:
    parsed = urllib.parse.urlparse(u)
    path = parsed.path.lower()
    if any(path.endswith(ext) for ext in valid_exts):
        asset_urls.append(u)

print(f"Filtered to {len(asset_urls)} true binary media assets.")

manifest = {}
success_count = 0
fail_count = 0

os.makedirs('public/assets', exist_ok=True)

# Timestamps to try in priority order (2004-2005 era)
timestamps = [
    '20040709114327',
    '20041229014632',
    '20040523164200',
    '20041129063037',
    '20050212175134',
    '20060826073613',
    '20081015174550'
]

for idx, url in enumerate(asset_urls):
    parsed = urllib.parse.urlparse(url)
    rel_path = parsed.netloc + parsed.path
    local_path = os.path.join('public/assets', rel_path.lstrip('/'))
    os.makedirs(os.path.dirname(local_path), exist_ok=True)

    # Check if already downloaded and not empty
    if os.path.exists(local_path) and os.path.getsize(local_path) > 100:
        manifest[url] = local_path.replace('public/', '/')
        print(f"[{idx+1}/{len(asset_urls)}] Already exists: {rel_path} ({os.path.getsize(local_path)} bytes)")
        success_count += 1
        continue

    # Try downloading with curl across timestamps
    downloaded = False
    for ts in timestamps:
        wb_url = f"https://web.archive.org/web/{ts}im_/{url}"
        try:
            res = subprocess.run(
                ['curl', '-L', '-s', '-f', '-m', '10', '-o', local_path, wb_url],
                capture_output=True
            )
            if res.returncode == 0 and os.path.exists(local_path) and os.path.getsize(local_path) > 100:
                downloaded = True
                break
        except Exception:
            pass

    # If direct timestamp failed, try query CDX to find exact snapshot
    if not downloaded:
        try:
            cdx_url = f"https://web.archive.org/cdx/search/cdx?url={urllib.parse.quote(url)}&filter=statuscode:200&output=json&limit=3"
            cdx_res = subprocess.run(['curl', '-s', '-m', '10', cdx_url], capture_output=True, text=True)
            if cdx_res.returncode == 0 and cdx_res.stdout.startswith('['):
                rows = json.loads(cdx_res.stdout)
                if len(rows) > 1:
                    exact_ts = rows[1][1]
                    exact_wb_url = f"https://web.archive.org/web/{exact_ts}im_/{url}"
                    sub_res = subprocess.run(
                        ['curl', '-L', '-s', '-f', '-m', '10', '-o', local_path, exact_wb_url],
                        capture_output=True
                    )
                    if sub_res.returncode == 0 and os.path.exists(local_path) and os.path.getsize(local_path) > 100:
                        downloaded = True
        except Exception as e:
            pass

    if downloaded:
        sz = os.path.getsize(local_path)
        manifest[url] = local_path.replace('public/', '/')
        print(f"[{idx+1}/{len(asset_urls)}] OK: {rel_path} ({sz} bytes)")
        success_count += 1
    else:
        # If download failed or file empty, delete empty artifact
        if os.path.exists(local_path) and os.path.getsize(local_path) <= 100:
            os.remove(local_path)
        print(f"[{idx+1}/{len(asset_urls)}] MISSING/FAILED: {url}")
        fail_count += 1

    time.sleep(0.1)

print(f"\nDone! Successfully downloaded: {success_count}, Failed/Missing: {fail_count}")

with open('public/asset_manifest.json', 'w') as f:
    json.dump(manifest, f, indent=2)
