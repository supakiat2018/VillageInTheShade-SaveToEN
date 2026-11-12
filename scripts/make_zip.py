import os
import shutil
import zipfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BIN_DIR = os.path.join(ROOT, "bin")
CFG_DIR = os.path.join(ROOT, "config")
OUT_DIR = os.path.join(ROOT, "output")
STAGE_DIR = os.path.join(OUT_DIR, "staging")
ZIP_PATH = os.path.join(OUT_DIR, "Village_SaveJP_to_EN_v1.0.zip")

def main():
    if os.path.exists(STAGE_DIR):
        shutil.rmtree(STAGE_DIR)
    mod_sub = os.path.join(STAGE_DIR, "Mods", "SaveJPtoEN")
    os.makedirs(mod_sub, exist_ok=True)

    shutil.copy2(os.path.join(BIN_DIR, "winmm.dll"), os.path.join(STAGE_DIR, "winmm.dll"))
    shutil.copy2(os.path.join(BIN_DIR, "save_jp_to_en.dll"), os.path.join(mod_sub, "save_jp_to_en.dll"))
    shutil.copy2(os.path.join(CFG_DIR, "save_jp_to_en.ini"), os.path.join(mod_sub, "save_jp_to_en.ini"))
    shutil.copy2(os.path.join(ROOT, "README.txt"), os.path.join(STAGE_DIR, "README_SaveJP_to_EN.txt"))

    with zipfile.ZipFile(ZIP_PATH, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=9) as zf:
        for folder, _, files in os.walk(STAGE_DIR):
            for fn in sorted(files):
                full = os.path.join(folder, fn)
                rel = os.path.relpath(full, STAGE_DIR)
                zf.write(full, rel)

    print(f"[OK] Staging folder: {STAGE_DIR}")
    print(f"[OK] ZIP created   : {ZIP_PATH} ({os.path.getsize(ZIP_PATH)} bytes)")

if __name__ == "__main__":
    main()
