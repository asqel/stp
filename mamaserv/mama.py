import packet as pck
import os
import hashlib

id_to_path: dict[int, str] = {}
new_id = 1

def find_id(root_dir: str, name: bytes) -> int:
    name_str = os.path.join(name.decode("utf-8"))
    for iid, path in id_to_path.items():
        if path == name_str:
            return iid

    if not os.path.isfile(os.path.join(root_dir, name_str)):
        return 0

    global new_id
    id_to_path[new_id] = name_str
    new_id += 1
    return new_id - 1

def send_info(root_dir: str, packet, id: int) -> None:
    try:
        p_name = id_to_path.get(id, "")
        if not p_name:
            packet._type = pck.ERR_INV_ID
            return

        name = p_name.encode("utf8")
        if (len(name) > 64):
            name = name[:64]
        name += b'\x00' * (64 - len(name))

        size = os.path.getsize(os.path.join(root_dir, p_name)).to_bytes(8, "little")
        version = int(0).to_bytes(4, "little")
        iszip = int(0).to_bytes(4, "little")

        md5sum = b''
        with open(os.path.join(root_dir, p_name), "rb") as f:
            md5 = hashlib.md5()
            while True:
                chunk = f.read(8192)
                if not chunk:
                    break
                md5.update(chunk)
            md5sum = md5.digest()

        desc = "no description".encode("utf-8")
        if (len(desc) > 1000):
            desc = desc[:1000]

        packet.append(name)
        packet.append(size)
        packet.append(version)
        packet.append(iszip)
        packet.append(md5sum)
        packet.append(desc)
        packet._type = pck.GET_INFO_RSP
        return

    except Exception as e:
        print(f" ffdffd{e}", type(e))
        packet._type = pck.ERR_FAIL
        return

def send_part(root_path: str, packet, id: int, offset: int, _len: int) -> None:
    try:
        p_name = id_to_path.get(id, "")
        if not p_name:
            packet._type = pck.ERR_INV_ID
            return
        if _len + 2 + 6 > pck.MAX_PACKET_SIZE:
            packet._type = pck.ERR_TOO_LONG
            return
        size = os.path.getsize(os.path.join(root_path, p_name))
        if offset >= size:
            packet._type = pck.ERR_OUT_RANGE
            return
        part = b''
        with open(os.path.join(root_path, p_name), "rb") as f:
            f.seek(offset, 0)
            part = f.read(_len)
        packet._type = pck.READ_PART_RSP
        packet.append(part)
        return

    except Exception as e:
        packet._type = pck.ERR_FAIL
        return
