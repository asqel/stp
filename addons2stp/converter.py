import urllib.request as urlreq
import os, sys, json

SHOW_CMD = False
OUTPUT_DIR = "output"

path = os.path.dirname(os.path.abspath(__file__))

def json_from_url(url):
    with urlreq.urlopen(url) as response:
        data = response.read()
    return json.loads(data)

try:
    addons_json = json_from_url("https://elydre.github.io/profan/addons.json")
except Exception as e:
    print("Failed to retrieve JSON data from URL:", e)
    sys.exit(1)

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


ADDONS = [e for category in addons_json["ADDONS"] for e in addons_json["ADDONS"][category]]
FILEARRAY = addons_json["FILEARRAY"]

output_dict = {}

exec(f"rm -rf {os.path.join(path, OUTPUT_DIR)} {os.path.join(path, 'tmp')}")
exec(f"mkdir {os.path.join(path, OUTPUT_DIR)}")

for i, addon in enumerate(ADDONS):
    print(f"{i+1}/{len(ADDONS)}: {addon['name']}")
    output_dict[addon["name"]] = [
        addon["description"],
        os.path.join(OUTPUT_DIR, addon['name'] + '.zip'),
        i + 1,
        [get_addon_index(dep) + 1 for dep in addon["dependencies"]] if "dependencies" in addon else [],
        0, # TODO: version
    ]

    exec(f"mkdir {os.path.join(path, 'tmp')}")

    for file in addon["files"]:
        print(f"  {file}")
        f = get_file_from_name(file)

        if not ("is_targz" in f and f["is_targz"]):
            profan_path = "/" + "/".join(f['profan_path'])

            exec(f"wget -q {f['url']} -O {os.path.join(path, 'tmp', f['name'])}")
            exec(f"echo \"mv -f {file} {profan_path}\" >> {os.path.join(path, 'tmp', 'install.olv')}")
            exec(f"echo \"rm -f {profan_path}\" >> {os.path.join(path, 'tmp', 'uninstall.olv')}")
            continue

        profan_path = "/" + "/".join(f['profan_path'])
        targz_path = os.path.join(path, "tmp", f"{f['name']}.tar.gz")

        exec(f"wget -q {f['url']} -O {targz_path}")

        exec(f"mkdir {os.path.join(path, 'tmp', f['name'])}")
        exec(f"tar -xf {targz_path} -C {os.path.join(path, 'tmp', f['name'])}")
        exec(f"rm -f {targz_path}")

        exec(f"echo \"rm -rf {profan_path}\" >> {os.path.join(path, 'tmp', 'install.olv')}")
        exec(f"echo \"mv {file} {profan_path}\" >> {os.path.join(path, 'tmp', 'install.olv')}")
        exec(f"echo \"rm -rf {profan_path}\" >> {os.path.join(path, 'tmp', 'uninstall.olv')}")

    exec(f"cd {os.path.join(path, 'tmp')} && zip -rq {os.path.join(path, OUTPUT_DIR, addon['name'] + '.zip')} ./*")
    exec(f"rm -rf {os.path.join(path, 'tmp')}")

with open(os.path.join(path, "output.json"), "w") as f:
    json.dump(output_dict, f, indent=4)
