/*
 *  SoapyPavelDeminSDR: Soapy SDR plug-in for Red Pitaya SDR transceiver
 *  Copyright (C) 2015  Pavel Demin
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <string>
#include <sstream>
#include <iostream>
#include <stdexcept>

#include <stdint.h>
#include <string.h>

#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#include <windows.h>
#define INVSOC INVALID_SOCKET
#else
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#ifndef SOCKET
#define SOCKET int
#define INVSOC (-1)
#endif
#endif

#include "SoapySDR/Device.hpp"
#include "SoapySDR/Registry.hpp"

#if defined(__APPLE__) || defined(__MACH__)
#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL SO_NOSIGPIPE
#endif
#endif

#define BUF_SIZE_BYTES 65536

using namespace std;

/***********************************************************************
 * Device interface
 **********************************************************************/

class SoapyPavelDeminSDR : public SoapySDR::Device
{
public:
    SoapyPavelDeminSDR(const SoapySDR::Kwargs &args):
        _addr("192.168.1.100"), _port(1001)
    {
        size_t i;

        #if defined(_WIN32)
        WSADATA wsaData;
        WSAStartup(MAKEWORD(2, 2), &wsaData);
        #endif

        _freq = 6.0e5;
        _freq_value = 600000;
        _rate = 192e3;
        _rate_value = 2;

        _socket = INVSOC;

        if(args.count("addr")) _addr = args.at("addr");
        if(args.count("port")) stringstream(args.at("port")) >> _port;

        _buf = malloc(BUF_SIZE_BYTES);
    }

    ~SoapyPavelDeminSDR()
    {
	free(_buf);

        #if defined(_WIN32)
        WSACleanup();
        #endif
    }

    /*******************************************************************
     * Identification API
     ******************************************************************/

    string getDriverKey() const
    {
        return "paveldeminsdr";
    }

    string getHardwareKey() const
    {
        return "paveldeminsdr";
    }

    /*******************************************************************
     * Channels API
     ******************************************************************/

    size_t getNumChannels(const int direction) const
    {
        if(direction == SOAPY_SDR_RX)
       		return 1;
	else if (direction == SOAPY_SDR_TX)
	       	return 0;
        return SoapySDR::Device::getNumChannels(direction);
    }

    /*******************************************************************
     * Stream API
     ******************************************************************/

    vector<string> getStreamFormats(const int direction, const size_t channel) const
    {
        vector<string> formats;
        formats.push_back("CF32");
        return formats;
    }

    string getNativeStreamFormat(const int direction, const size_t channel, double &fullScale) const
    {
        fullScale = 1.0;
        return "CF32";
    }

    SoapySDR::Stream *setupStream(
        const int direction,
        const string &format,
        const vector<size_t> &channels = vector<size_t>(),
        const SoapySDR::Kwargs &args = SoapySDR::Kwargs())
    {
        if(format != "CF32") throw runtime_error("setupStream invalid format " + format);

        return (SoapySDR::Stream *)(new int(direction));
    }

    void closeStream(SoapySDR::Stream *stream)
    {
        delete (int *)stream;
    }

    int activateStream(
        SoapySDR::Stream *stream,
        const int flags = 0,
        const long long timeNs = 0,
        const size_t numElems = 0)
    {
        int direction = *(int *)stream;

        double frequency = 0.0, rate = 0.0;
        
        std::cout << "In activateStream" << std::endl;

        if(direction == SOAPY_SDR_TX)
        {
            return SOAPY_SDR_NOT_SUPPORTED;
        }
        
        if(_socket != INVSOC)
        {
            std::cout << "Alread initialized, nothing to do" << std::endl;
            return 0;
        }
        
        frequency = _freq;
        rate = _rate;
        
        _socket = openConnection();
        
        std::cout << "Initialized" << std::endl;
        
        if(_socket == INVSOC)
        {
            std::cout << "socket is invalid" << std::endl;
        }
        
        setFrequency(direction, 0, "RF", frequency);
        setSampleRate(direction, 0, rate);

        return 0;
    }

    int deactivateStream(
        SoapySDR::Stream *stream,
        const int flags = 0,
        const long long timeNs = 0)
    {
        int direction = *(int *)stream;

        if(direction == SOAPY_SDR_RX)
        {
            #if defined(_WIN32)
            ::closesocket(_socket);
            #else
            ::close(_socket);
            #endif

            _socket = INVSOC;
        }

        return 0;
    }

    int readStream(
        SoapySDR::Stream *stream,
        void * const *buffs,
        const size_t numElems,
        int &flags,
        long long &timeNs,
        const long timeoutUs = 100000)
    {
        struct timeval timeout;
        int items_fetched = 0;
        float *out = (float *)buffs[0];
        float *tmp = (float *)_buf;

        #if defined(_WIN32)
        u_long total = 8 * 2 * sizeof(float) * (u_long)numElems;
        if (total > BUF_SIZE_BYTES)
            total = BUF_SIZE_BYTES;
        u_long size = 0;
        //::ioctlsocket(_socket, FIONREAD, &size);
        #else
        int total = 8 * 2 * sizeof(float) * numElems;
        if (total > BUF_SIZE_BYTES)
            total = BUF_SIZE_BYTES;
        int size = 0;
        //::ioctl(_socket, FIONREAD, &size);
        #endif

#if 0        
        if(size < total)
        {
            timeout.tv_sec = timeoutUs / 1000000;
            timeout.tv_usec = timeoutUs % 1000000;

            ::select(0, 0, 0, 0, &timeout);

            #if defined(_WIN32)
            ::ioctlsocket(_socket, FIONREAD, &size);
            #else
            ::ioctl(_socket, FIONREAD, &size);
            #endif
        }

        if(size < total) return SOAPY_SDR_TIMEOUT;
#endif
        #if defined(_WIN32)
        size = ::recv(_socket, (char *)_buf, total, MSG_WAITALL);
        #else
        size = ::recv(_socket, _buf, total, MSG_WAITALL);
        #endif
        
        if(size != total)
        {
            throw runtime_error("Receiving samples failed.");
        }

        items_fetched = size / (8 * 2 * sizeof(float));
  
        for(int kk=0; kk<(2*items_fetched);kk=kk+2){
    	    out[kk  ]=tmp[8*kk  ];
    	    out[kk+1]=tmp[8*kk+1];
        }

        return items_fetched;
    }

    int writeStream(
        SoapySDR::Stream *stream,
        const void * const *buffs,
        const size_t numElems,
        int &flags,
        const long long timeNs = 0,
        const long timeoutUs = 100000)
    {
        return SOAPY_SDR_NOT_SUPPORTED;
    }

    /*******************************************************************
     * Frequency API
     ******************************************************************/

    void setFrequency(const int direction, const size_t channel, const string &name, const double frequency, const SoapySDR::Kwargs &args = SoapySDR::Kwargs())
    {
	uint32_t buffer[10] = {0,0,0,0,0,0,0,0,0,0};

        if (_socket == INVSOC)
            std::cout << "in setFrequency: Socket not open" << std::endl;
    
        //if(name == "BB") return;
        if(name != "RF") throw runtime_error("setFrequency invalid name " + name);

        _freq_value = (uint32_t)floor(frequency + 0.5);

        if(direction == SOAPY_SDR_RX)
        {
            if(frequency < _rate / 2.0 || frequency > 3.0e7)
                return;
            buffer[1] = _rate_value;
            for(int kk=2; kk<10; kk++)
                buffer[kk]=_freq_value;

            sendCommand(_socket, buffer);

            _freq = frequency;
        }
    }

    double getFrequency(const int direction, const size_t channel, const string &name) const
    {
        double frequency = 0.0;

        //if(name == "BB") return 0.0;
        if(name != "RF") throw runtime_error("getFrequency invalid name " + name);

        if(direction == SOAPY_SDR_RX)
        {
            frequency = _freq;
        }

        return frequency;
    }

    vector<string> listFrequencies(const int direction, const size_t channel) const
    {
        vector<string> names;
        names.push_back("RF");
        return names;
    }

    SoapySDR::RangeList getFrequencyRange(const int direction, const size_t channel, const string &name) const
    {
        double rate = 0.0;

        //if (name == "BB") return SoapySDR::RangeList(1, SoapySDR::Range(0.0, 0.0));
        if (name != "RF") throw runtime_error("getFrequencyRange invalid name " + name);

        if(direction == SOAPY_SDR_RX)
        {
            rate = _rate;
        }

        return SoapySDR::RangeList(1, SoapySDR::Range(rate / 2.0, 30.0e6));
    }

    /*******************************************************************
     * Sample Rate API
     ******************************************************************/

    void setSampleRate(const int direction, const size_t channel, const double rate)
    {
	uint32_t buffer[10] = {0,0,0,0,0,0,0,0,0,0};
    
        if (_socket == INVSOC)
            std::cout << "in setSampleRate: Socket not open" << std::endl;

        if(48e3 == rate) _rate_value = 0;
        else if(96e3 == rate) _rate_value = 1;
        else if(192e3 == rate) _rate_value = 2;
        else if(384e3 == rate) _rate_value = 3;

        if(direction == SOAPY_SDR_RX)
        {
            buffer[1] = _rate_value;
            for(int kk=2; kk<10; kk++)
                buffer[kk]=_freq_value;

            sendCommand(_socket, buffer);

            _rate = rate;
        }
    }

    double getSampleRate(const int direction, const size_t channel) const
    {
        double rate = 0.0;

        if(direction == SOAPY_SDR_RX)
        {
            rate = _rate;
        }

        return rate;
    }

    vector<double> listSampleRates(const int direction, const size_t channel) const
    {
        vector<double> rates;
        rates.push_back(48e3);
        rates.push_back(96e3);
        rates.push_back(192e3);
        rates.push_back(384e3);
        return rates;
    }

private:
    string _addr;
    unsigned short _port;
    double _freq, _rate;
    unsigned int _freq_value, _rate_value;
    SOCKET _socket;
    void *_buf;

    SOCKET openConnection()
    {
        stringstream message;
        struct sockaddr_in addr;
        fd_set writefds;
        struct timeval timeout;
        int result;
        SOCKET socket;
        
        std::cout << "In openConnection" << std::endl;

        socket = ::socket(AF_INET, SOCK_STREAM, 0);
        if(socket == INVSOC)
        {
            throw runtime_error("SoapyPavelDeminSDR could not create TCP socket");
        }

#if 0
        /* enable non-blocking mode */

        #if defined(_WIN32)
        u_long mode = 1;
        ::ioctlsocket(socket, FIONBIO, &mode);
        #else
        int flags = ::fcntl(socket, F_GETFL, 0);
        ::fcntl(socket, F_SETFL, flags | O_NONBLOCK);
        #endif
#endif
        memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        inet_pton(AF_INET, _addr.c_str(), &addr.sin_addr);
        addr.sin_port = htons(_port);

        result = ::connect(socket, (struct sockaddr *)&addr, sizeof(addr));
        if(result < 0)
        {
            message << "SoapyPavelDeminSDR could not connect to " << _addr << ":" << _port;
            throw runtime_error(message.str());
        }
#if 0
        timeout.tv_sec = 5;
        timeout.tv_usec = 0;

        FD_ZERO(&writefds);
        FD_SET(socket, &writefds);

        #if defined(_WIN32)
        result = ::select(0, 0, &writefds, 0, &timeout);
        #else
        result = ::select(socket + 1, 0, &writefds, 0, &timeout);
        #endif

        if(result <= 0)
        {
            message << "SoapyPavelDeminSDR could not connect to " << _addr << ":" << _port;
            throw runtime_error(message.str());
        }

        /* disable non-blocking mode */

        #if defined(_WIN32)
        mode = 0;
        ::ioctlsocket(socket, FIONBIO, &mode);
        #else
        flags = ::fcntl(socket, F_GETFL, 0);
        ::fcntl(socket, F_SETFL, flags & ~O_NONBLOCK);
        #endif
#endif        
        return socket;
    }

    void sendCommand(SOCKET socket, uint32_t *bufptr)
    {
        stringstream message;

        if(socket == INVSOC){
            std::cout << "in sendCommand: socket not open" << std::endl;
            return;
        } 

        #if defined(_WIN32)
        int total = 10 * sizeof(uint32_t);
        int size;
        size = ::send(socket, (char *)bufptr, total, 0);
        #else
        ssize_t total = 10 * sizeof(uint32_t);
        ssize_t size;
        size = ::send(socket, bufptr, total, MSG_NOSIGNAL);
        #endif

        if(size < total)
        {
            message << "sendCommand failed.";
            throw runtime_error(message.str());
        }
    }
};

/***********************************************************************
 * Find available devices
 **********************************************************************/
SoapySDR::KwargsList findSoapyPavelDeminSDR(const SoapySDR::Kwargs &args)
{
    vector<SoapySDR::Kwargs> results;

    //the user explicitly specified the paveldeminsdr driver
    if (args.count("driver") != 0 and args.at("driver") == "paveldeminsdr")
    {
        //TODO perform a test connection to validate device presence
        results.push_back(args);
        return results;
    }

    //the user only passed an address
    if (args.count("addr") != 0)
    {
        //TODO this could refer to an address for other drivers
        //we need to test connect before its safe to yield
        results.push_back(args);
        return results;
    }

    //otherwise, perform a discovery for devices on the LAN
    //TODO
    results.push_back(args);
    return results;
}

/***********************************************************************
 * Make device instance
 **********************************************************************/
SoapySDR::Device *makeSoapyPavelDeminSDR(const SoapySDR::Kwargs &args)
{
    return new SoapyPavelDeminSDR(args);
}

/***********************************************************************
 * Registration
 **********************************************************************/
static SoapySDR::Registry registerSoapyPavelDeminSDR("paveldeminsdr", &findSoapyPavelDeminSDR, &makeSoapyPavelDeminSDR, SOAPY_SDR_ABI_VERSION);
