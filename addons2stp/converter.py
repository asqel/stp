import urllib.request as urlreq
from datetime import datetime
import os, sys, json
import hashlib

SHOW_CMD = False
FORCE_UPDATE = False
OUTPUT_DIR = "output"

path = os.path.dirname(os.path.abspath(__file__))

def json_from_url(url):
    try:
        with urlreq.urlopen(url) as response:
            data = response.read()
        return json.loads(data)
    except Exception as e:
        print("Failed to retrieve JSON data from URL:", e)
        sys.exit(1)

def json_from_file(path, default={}):
    try:
        with open(path, "r") as f:
            return json.load(f)
    except Exception:
        print(f"Warning: failed to read JSON from '{path}'")
        return default

def get_addon_index(addon_name):
    for i, addon in enumerate(ADDONS):
        if addon["name"] == addon_name:
            return i
    print(f"Error: Dependency '{addon_name}' not found in ADDONS list.")
    exit(1)

def get_file_from_name(file_name):
    for file in FILEARRAY:
        if file["name"] == file_name:
            return file
    print(f"Error: File '{file_name}' not found in FILEARRAY.")
    exit(1)

def exec(command):
    try:
        shell_len = os.get_terminal_size().columns
    except Exception:
        shell_len = 180

    if SHOW_CMD:
        if len(command) > shell_len:
            print(f"\033[90m{command[:shell_len - 3]}...\033[0m")
        else:
            print(f"\033[90m{command}\033[0m")

    code = os.system(command) >> 8

    if code != 0:
        print(f"command '{command}' failed with code {code}")
        os._exit(code)

def dosum(path):
    with open(path, "rb") as f:
        data = f.read()
        return hashlib.sha256(data).hexdigest()

def is_same_file(path, name):
    nsum = dosum(path)
    nsum_dict[name] = nsum

    osum = OLD_SUMD[name] if name in OLD_SUMD else None

    if osum is None:
        print(f"  {name:15}  [++]")
        return 0

    if nsum != osum:
        print(f"  {name:15}  [!=]")
        return 0

    print(f"  {name:15}  [==]")
    return 1

def gen_version():
    # get GMT time
    now = datetime.utcnow()
    return (now.year - 2000) * 100000 + now.timetuple().tm_yday * 100 + now.hour

addons_json = json_from_url("https://elydre.github.io/profan/addons.json")
sum_json    = json_from_file("sums.json")

ADDONS = [e for category in addons_json["ADDONS"] for e in addons_json["ADDONS"][category]]
FILEARRAY = addons_json["FILEARRAY"]

OLD_SUMD = sum_json["SUMS"] if "SUMS" in sum_json else {}
OLD_VERS = sum_json["VERSIONS"] if "VERSIONS" in sum_json else {}

NEW_VER = gen_version()

output_dict = {}
nsum_dict   = {}
nver_dict   = {}

exec(f"rm -rf {os.path.join(path, OUTPUT_DIR)} {os.path.join(path, 'tmp')}")
exec(f"mkdir {os.path.join(path, OUTPUT_DIR)}")

for i, addon in enumerate(ADDONS):
    print(f"{i+1}/{len(ADDONS)}: {addon['name']}")

    exec(f"mkdir {os.path.join(path, 'tmp')}")
    is_same_version = 1

    install_file = open(os.path.join(path, "tmp", "install.olv"), "w")
    remove_file  = open(os.path.join(path, "tmp", "remove.olv"), "w")

    install_file.write("#!/bin/f/olivine.elf\n\n")
    remove_file.write("#!/bin/f/olivine.elf\n\n")

    for file in addon["files"]:
        f = get_file_from_name(file)

        if not ("is_targz" in f and f["is_targz"]):
            profan_path = "/" + "/".join(f['profan_path'])

            exec(f"wget -q {f['url']} -O {os.path.join(path, 'tmp', f['name'])}")

            is_same_version *= is_same_file(os.path.join(path, "tmp", f['name']), f['name'])

            install_file.write(f"echo -- '+ {profan_path}'\n")
            install_file.write(f"mkdir -p '{os.path.dirname(profan_path)}'\n")
            install_file.write(f"mv -f '{file}' '{profan_path}'\n\n")

            remove_file.write(f"echo -- '- {profan_path}'\n")
            remove_file.write(f"rm -f '{profan_path}'\n\n")

            continue

        profan_path = "/" + "/".join(f['profan_path'])
        targz_path = os.path.join(path, "tmp", f"{f['name']}.tar.gz")

        exec(f"wget -q {f['url']} -O {targz_path}")

        is_same_version *= is_same_file(targz_path, f['name'])

        exec(f"mkdir {os.path.join(path, 'tmp', f['name'])}")
        exec(f"tar -xf {targz_path} -C {os.path.join(path, 'tmp', f['name'])}")
        exec(f"rm -f {targz_path}")

        for e in os.listdir(os.path.join(path, "tmp", f['name'])):
            install_file.write(f"echo -- '+ {profan_path}/{e}'\n")
            install_file.write(f"mkdir -p '{profan_path}'\n")
            install_file.write(f"mv -f '{file}/{e}' '{profan_path}/{e}'\n")

            remove_file.write(f"echo -- '- {profan_path}/{e}'\n")
            remove_file.write(f"rm -rf '{profan_path}/{e}'\n")

        install_file.write("\n")
        remove_file.write("\n")

    install_file.close()
    remove_file.close()

    exec(f"cd {os.path.join(path, 'tmp')} && zip -rq {os.path.join(path, OUTPUT_DIR, addon['name'] + '.zip')} ./*")
    exec(f"rm -rf {os.path.join(path, 'tmp')}")

    # update version

    if is_same_version and addon["name"] in OLD_VERS and not FORCE_UPDATE:
        version = OLD_VERS[addon["name"]]
    else:
        version = gen_version()

    print(f"  => {'same' if is_same_version else 'new'} version")

    nver_dict[addon["name"]] = version

    # append to output dict

    output_dict[addon["name"]] = [
        addon["description"],
        os.path.join(OUTPUT_DIR, addon['name'] + '.zip'),
        i + 1,
        [get_addon_index(dep) + 1 for dep in addon["dependencies"]] if "dependencies" in addon else [],
        version,
        1 # zip package format
    ]

# add unzip.elf as a single file

print("unzip.elf")

exec(f"wget -q https://github.com/elydre/libatron/releases/download/latest/unzip.elf -O {OUTPUT_DIR}/unzip.elf")
is_same_version = is_same_file(f"{OUTPUT_DIR}/unzip.elf", "unzip.elf")

if is_same_version and "_unzip" in OLD_VERS and not FORCE_UPDATE:
    version = OLD_VERS["_unzip"]
else:
    version = gen_version()

output_dict["unzip.elf"] = [
    "Unix unzip command",
    os.path.join(OUTPUT_DIR, "unzip.elf"),
    len(ADDONS) + 1,
    [],
    version,
    0 # raw file format
]

with open(os.path.join(path, "output.json"), "w") as f:
    json.dump(output_dict, f, indent=4)

with open(os.path.join(path, "sums.json"), "w") as f:
    json.dump({
        "SUMS": nsum_dict,
        "VERSIONS": nver_dict
    }, f, indent=4)
