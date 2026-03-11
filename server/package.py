import json
import os
import shutil

packages: dict[str, list[str, str, int]] = {}; # name: [desc, path, id]
id_to_name: dict[int, str] = {}

def init_packages() -> None:
	global packages
	with open("./packages.json", "rb") as f:
		packages = json.load(f);

def exit_packages() -> None:
	with open("./packages.json.new", "wb") as f:
		json.dump(packages, f);
	shutil.copyfile("./packages.json.new", "./packages.json");
	os.remove("./packages.json.new");

def find_id(name: bytes) -> int:
	try:
		id = packages[name.decode("utf-8")][2];
		id_to_name[id] = name
		return id;
	except:
		return 0;
