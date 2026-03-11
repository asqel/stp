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

def send_info(packet: packet.packet_t, id: int) -> None:
	try:
		p_name = id_to_name.get(id, "");
		if not p_name:
			packet._type = packet.ERR_INV_ID
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

def send_part(packet: packet.packet_t, id: int, offset: int, _len: int) -> None:
	try:
		p_name = id_to_name.get(id, "");
		if not p_name:
			packet._type = packet.ERR_INV_ID
			return ;
		if _len + 2 + 6 > packet.MAX_PACKET_SIZE:
			packet._type = packet.ERR_TOO_LONG;
			return ;
		size = os.path.getsize(packages[p_name][1]);
		if offset >= size:
			packet._type = packet.ERR_OUT_RANGE;
			return ;
		part = b'';
		with open(packages[p_name][1], "rb") as f:
			f.seek(offset, 0);
			part = f.read(_len);
		packet._type = packet.READ_PART_RSP;
		packet.append(part);
		return ;
		
	except:
		packet._type = packet.ERR_FAIL;
		return ;
