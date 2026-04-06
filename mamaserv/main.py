import socket as sock
import select
import packet

TIMEOUT = 5 * 60
PORT = 42024

def treat_packet(_packet):
	ip_port = _packet[1];
	_id = 0;
	_type = 0;
	_data = b'';
	if len(_packet[0]) < 8:
		return ;
	_id = int.from_bytes(_packet[0][:6], "little");
	_type = int.from_bytes(_packet[0][6:8], "little");
	_data = _packet[0][8:];

	_packet = packet.packet_t(ip_port[0], ip_port[1], _id, _type, _data);
	packet.respond(_packet);

def main() -> None:
	server_fd = sock.socket(sock.AF_INET, sock.SOCK_DGRAM, 0);
	server_fd.bind(('', PORT));
	print(f"Server listening on {PORT}");
	while 1:
		try:
			readable = [];
			writable = [];
			_ = [];
			if (packet.packets_to_send):
				readable, writable, _ = select.select([server_fd], [server_fd], [], TIMEOUT)
			else:
				readable, writable, _ = select.select([server_fd], [], [], TIMEOUT)

			if (readable):
				treat_packet(server_fd.recvfrom(packet.MAX_PACKET_SIZE));
			if (writable):
				server_fd.sendto(*packet.packets_to_send.pop(0).to_socket_packet());
		except InterruptedError:
			...

if __name__ == '__main__':
	try:
		main();
	except KeyboardInterrupt:
		print("\nclosing stp server");
	except Exception as e:
		print(e, type(e));
