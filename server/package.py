import json
import packet
import os
import shutil

packages: dict[str, list[str, str, int]] = {}; # name: [desc, path, id]
id_to_name: dict[int, str] = {}

def init_packages() -> None:
	global packages
	with open("./packages.json", "r") as f:
		packages = json.load(f);

def exit_packages() -> None:
	with open("./packages.json.new", "w") as f:
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

def send_info(packet: packet.packet_t, id: int) -> bytes:
	try:
		try:
			p_name = id_to_name[id];
		except:
			packet._type = packet.ERR_INV_ID;
			return ;

		size = os.path.getsize(packages[p_name][1]);
		desc = packages[p_name][0];
		packet._type = packet.GET_INFO_RSP;
		packet.append(size.to_bytes(8, "little"));
		if (len(desc) > 1000):
			desc = desc[:1000]
		packet.append(desc);
		return ;
	except:
		packet._type = packet.ERR_FAIL;
		return ;
