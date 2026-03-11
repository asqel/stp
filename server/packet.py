import package

MAX_PACKET_SIZE = 1300

packets_to_send: list[tuple[bytes, tuple[str, int]]] = [];

def send_packet(ip, port, data):
	packets_to_send.append((data, (ip, port)));

class packet_t:
	def __init__(self, ip, port, _id, _type, data = b''):
		self.ip = ip;
		self.port = port;
		self.data = data;
		self._id = _id;
		self._type = _type;
	
	def append(self, data):
		self.data += data;
	
	def to_socket_packet(self) -> tuple[bytes, tuple[str, int]]:
		data_res = self._id.to_bytes(6, "little");
		data_res += self._type.to_bytes(2, "little");
		data_res += self.data;
		return (data_res, (self.ip, self.port));
	
ERR_UNKNOWN_REQUEST = 0xFFFF;
ERR_UPDATING = 0xFF00;
ERR_INV_ID = 0xFF01;
ERR_OUT_RANGE = 0xFF03;
ERR_TOO_LONG = 0xFF04;
ERR_FAIL = 0xFF05

GET_ID = 0x01;
GET_ID_RSP = 0x02;

GET_INFO = 0x03;
GET_INFO_RSP = 0x04;

READ_PART = 0x07;
READ_PART_RSP = 0x08;

GET_DEP = 0x09;
GET_DEP_RSP = 0x0A;

def respond(request: packet_t) -> None:
	res: packet_t = packet_t(request.ip, request.port, request._id, 0);
	if (request._type == GET_ID):
		package_id = package.find_id(request.data);
		res._type = GET_ID_RSP;
		res.append(package_id.to_bytes(8, "little"));

	elif (request._type == GET_INFO):
		if (len(request.data) != 8):
			res._type = ERR_UNKNOWN_REQUEST;
		else:
			package.send_info(res, int.from_bytes(request.data, "little"));

	elif (request._type == READ_PART):
		if (len(request.data) != (8 + 8 + 2)):
			res._type = ERR_UNKNOWN_REQUEST;
		else:
			package.send_part(
				res,
				int.from_bytes(request.data[:8], "little"),
				int.from_bytes(request.data[8:16], "little"),
				int.from_bytes(request.data[16:], "little")
			);

	else:
		res._type = ERR_UNKNOWN_REQUEST;

	packets_to_send.append(res);
