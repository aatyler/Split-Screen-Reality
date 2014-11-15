#include <iostream>
#include "xbeeDMapi.h"
#include "TTYserial.h"
#include "xbmain.h"
#include "stopwatch.h"
#include <mutex>
#include <thread>
#include <vector>
#include <iterator>
#include <string>
#include <cstdint>
#include <unistd.h>
#include <stdio.h>
#include <time.h>
#include <queue>
#include <cstdint>
#include <string>
#include <stdio.h>
#include <stdlib.h>


int main(int argc, char* argv[])
{
	bool SLAVEMODE = false;
	if (argc > 1) // Handle proram arguments. 
	{
		for (int i = 1; i < argc; i++)
		{
			if (argv[i][0] == '-')
			{
				if (argv[i][1] == 's') SLAVEMODE = true;
				if (argv[i][1] == 'm') SLAVEMODE = false;
				if (argv[i][1] == 'h')
				{
					std::cout << "Useage: " << argv[0] << " -[smh] [modem]\n";
					std::cout << "NOTE: program defaults to master mode with a modem at\n";
					std::cout << modem << std::endl;
					std::cout << "Further, device speed is locked at 57600 baud.\n";
				}
			}
			else modem = argv[i];
		}
	}

	std::string port(TTYMonitor_main);
	START = true;

	while (TTYStarted == false) {}

	if (TTYStartFailed) 
	{
		std::cout << "TTY failure, modem was " << modem << std::endl;
		if (port.joinable()) port.join();
		return 0;
	}

	if (SLAVEMODE) slaveMain(modem);
	else masterMain(modem);

	if (port.joinable()) port.join();

	std::cout << "All threads ended nicely.\n";
	return 0;
}

void TTYMonitor_main(void)
{
	TTYserial tty;
	tty.begin(modem, baud);
	
	if (!(tty.status()))
	{
		std::cout << "*******************ERROR OPENING SERIAL PORT*******************\n";
		TTYStarted = true;
		TTYStartFailed = true;
		return;
	}

	while (!(START)) {}

	while (!(STOP))
	{
		if (tty.available() && STOP == false)
		{
			inBytesMutex.lock();
			inBytes.push_back(tty.readbyte());
			inBytesMutex.unlock();
		}

		if (outBytes.size() && STOP == false)
		{
			outBytesMutex.lock();
			tty.sendbyte(outBytes.front());
			usleep(1);
			outBytes.pop_front();
			outBytesMutex.unlock();
		}
	}

	return;
}

void slaveMain(std::string m)
{

	//stuff
	return;
}

void masterMain(std::string m)
{
	xbeeDMapi xb;
	stopwatch ATNDResend;

	std::cout << "Starting master main." << std::endl;
	NMAP_stopwatch.reset();

	while (!(STOP))
	{
		ATNDResend.reset();
		xb.ATND();
		xb.sendPkt();

		while (videoBuffer.size()) videoBuffer.pop();

		bool GOTNEIGHBOR = false;
		while(GOTNEIGHBOR == false && ATNDResend.read() < 10*1000)
		{
			if (xb.pktAvailable())
			{
				rcvdPacket pkt;
				xbeeDMapi::zeroPktStruct(pkt);
				xb.rcvPkt(pkt);

				if (pkt.pType == APIid_ATCR)
				{
					if(pkt.ATCmd[0] == 'N' && pkt.ATCmd[1] == 'D')
					{
						NMAP.update(pkt.from);
						GOTNEIGHBOR = true;
					}
				}
			}
		}

		if (GOTNEIGHBOR)
		{
			if (neighborIndex < NMAP.neighborCount() && NMAP.neighborCount > 0)
			{
				currentNeighbor = NMAP[neighborIndex];

				SSRPacketCreator outgoingPacket(SSRPT_videoRequest);

				xb.makeUnicastPkt(currentNeighbor);
				xb.loadUnicastPkt(outgoingPacket.get());
				xb.sendPkt;

				stopwatch videoTimeout;

				bool KEEPWAITING = true;
				bool FULLSEGMENT = false;

				while(KEEPWAITING)
				{
					if (videoTimeout.read() >= 10*1000) 
					{
						KEEPWAITING = false;
						while (videoBuffer.size()) videoBuffer.pop();
					}

					else
					{
						if (xb.pktAvailable())
						{
							rcvdPacket pkt;
							xbeeDMapi::zeroPktStruct(pkt);
							xb.rcvPkt(pkt);

							if (pkt.pType == APIid_ATCR)
							{
								std::cout << "Received ATCR message packet: ";
								if (pkt.ATCmd[0] == 'N' && pkt.ATCmd[1] == 'D') 
								{
									NMAP.update(pkt.from);
									std::cout << "hello message.\n";
								}
							}

							else if (pkt.pType == APIid_RP && pkt.from == currentNeighbor)
							{
								if (pkt.data[0] & (1<<SSRPT_videoPacket))
								{
									//std::cout << "Received a video packet from current neighbor.\n";
									videoTimeout.reset();
									pkt.data.erase(pkt.data.begin());
									videoBuffer.push(pkt.data);
									if (pkt.data.size() < 71)
									{
										KEEPWAITING = false;
										FULLSEGMENT = true;
									}
								}
							}

							else
							{
								std::cout << "Received other packet, ignoring.\n";
							}
						}
					}

				}

				if (FULLSEGMENT)
				{
					int fd = open("inVideo", O_WRONLY | O_CREAT);
					if (fd <= 0) 
					{
						std::cout << "Video received but found file opening failure.\n";
						while(videoBuffer.size()) videoBuffer.pop();
					}

					else
					{
						while (videoBuffer.size())
						{
							std::vector<uint8_t> v = videoBuffer.front();
							videoBuffer.pop();
							uint8_t byte = 0x00;
							for (std::vector<uint8_t>::iterator it = v.begin(); it != v.end(); it++)
							{
								byte = v.front();
								write(fd, &byte,1);
							}
						}

						close(fd);

						system("omxplayer -f 15 inVideo");
					}

					
				}

				neighborIndex++;
				if (neighborIndex >= NMAP.neighborCount) neighborIndex = 0;

				if (NMAP_stopwatch.read() >= (120*1000) && NMAP.neighborCount() > 0)
				{
					NMAP.clear();	
					std::cout << "Network Map reset after timeout.\n";
				} 
			}

			else 
			{
				neighborIndex = 0;
				std::cout << "neighborIndex was reset to zero. \n";
			}
		}
	}
}

bool SSRPacketCreator::create(uint8_t type)
{
	if (type < 1 || type > 5) return false;
	clear();
	_type |= (1<<type);
	return true;
}

bool SSRPacketCreator::load(std::vector<uint8_t> p)
{
	if (p.size() == 0 || p.size() > 71) return false;

	_p = p;
	return true;
}

bool SSRPacketCreator::check(void)
{
	if (_p.size < 72) return true;
	else return false;
}

std::vector<uint8_t> SSRPacketCreator::get(void)
{
	std::vector<uint8_t> tv;
	tv.push_back(_type);
	if(_p.size() > 0)
	{
		for(std::vector<uint8_t>::iterator it = _p.begin(); it != _p.end(); it++)
		{
			tv.push_back(*it);
		}
	}

	return tv;
}