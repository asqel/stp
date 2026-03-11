import json
import packet as pck
import os
import shutil

packages: dict[str, list[str, str, int, list[int]]] = {}; # name: [desc, path, id, [dependencies]]
id_to_name: dict[int, str] = {}

def init_packages() -> None:
	global packages
	with open("./packages.json", "r") as f:
		packages = json.load(f);
	for i, k in packages.items():
		id_to_name[k[2]] = i;
		if len(k[3]) * 8 + 6 + 2 > pck.MAX_PACKET_SIZE:
			print("package {i} dependencies will be truncated");
			max_len = pck.MAX_PACKET_SIZE - 6 - 2;
			max_len = max_len // 8;
			k[3] = k[3][:max_len];

def exit_packages() -> None:
	with open("./packages.json.new", "w") as f:
		json.dump(packages, f);
	shutil.copyfile("./packages.json.new", "./packages.json");
	os.remove("./packages.json.new");

def find_id(name: bytes) -> int:
	try:
		id = packages[name.decode("utf-8")][2];
		id_to_name[id] = name.decode("utf-8");
		return id;
	except:
		return 0;

def send_info(packet: pck.packet_t, id: int) -> None:
	try:
		p_name = id_to_name.get(id, "");
		if not p_name:
			packet._type = pck.ERR_INV_ID
			return ;

		size = os.path.getsize(packages[p_name][1]);
		desc = packages[p_name][0].encode("utf-8");
		packet._type = pck.GET_INFO_RSP;
		packet.append(size.to_bytes(8, "little"));
		if (len(desc) > 1000):
			desc = desc[:1000]
		packet.append(desc);
		return ;

	except Exception as e:
		packet._type = pck.ERR_FAIL;
		return ;

def send_part(packet: pck.packet_t, id: int, offset: int, _len: int) -> None:
	try:
		p_name = id_to_name.get(id, "");
		if not p_name:
			packet._type = pck.ERR_INV_ID
			return ;
		if _len + 2 + 6 > pck.MAX_PACKET_SIZE:
			packet._type = pck.ERR_TOO_LONG;
			return ;
		size = os.path.getsize(packages[p_name][1]);
		if offset >= size:
			packet._type = pck.ERR_OUT_RANGE;
			return ;
		part = b'';
		with open(packages[p_name][1], "rb") as f:
			f.seek(offset, 0);
			part = f.read(_len);
		packet._type = pck.READ_PART_RSP;
		packet.append(part);
		return ;
		
	except Exception as e:
		packet._type = pck.ERR_FAIL;
		return ;
	
def send_dep(packet: pck.packet_t, id: int) -> None:
	try:
		p_name = id_to_name.get(id, "");
		if not p_name:
			packet._type = pck.ERR_INV_ID
			return ;
		dep = b'';
		for i in packages[p_name][3]:
			dep += i.to_bytes(8, "little");
		packet.append(dep);
		dep._type = pck.GET_DEP_RSP;
		return ;
	except Exception as e:
		packet._type = pck.ERR_FAIL;
		return ;
