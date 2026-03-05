import packages

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
	

def respond(request: packet_t) -> None:
	...	
