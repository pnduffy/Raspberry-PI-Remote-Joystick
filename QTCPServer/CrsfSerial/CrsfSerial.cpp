#include "CrsfSerial.h"

#include <unistd.h>
#include <fcntl.h>
#include <asm/termbits.h>
#include <sys/ioctl.h>
#include <stdio.h>
#include <string.h>
#include <QtGlobal>

unsigned long millis()
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (ts.tv_sec * 1000 + ts.tv_nsec / 1000000L);
}

long map(long x, long in_min, long in_max, long out_min, long out_max)
{
    return (x-in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}

CrsfSerial::CrsfSerial(const char path[], bool blocking, uint32_t baud, uint8_t timeout) :
     _crc(0xd5), _baud(baud),
    _lastReceive(0), _lastChannelsPacket(0), _linkIsUp(false),
    _passthroughMode(false)
{
    // Crsf serial is 420000 baud for V2
    _port = open(path, O_RDWR | O_NOCTTY | (blocking ? 0 : O_NONBLOCK));
    if (_port < 0)
    {
        return;
    }

    struct termios2 options;
    if (ioctl(_port, TCGETS2, &options))
    {
        close(_port);
        _port = -1;
        return;
    }

    // sbus options
    // see man termios(3)

    options.c_cflag &= ~PARENB;  // disable enable parity
    options.c_cflag &= ~CSTOPB;  // 1 stop bits
    options.c_cflag &= ~CSIZE;  // clear character size mask
    options.c_cflag |= CS8;     // 8 bit characters
    options.c_cflag &= ~CRTSCTS;  // disable hardware flow control
    options.c_cflag |= CREAD;   // enable receiver
    options.c_cflag |= CLOCAL;  // ignore modem lines

    options.c_lflag &= ~ICANON;  // receive characters as they come in
    options.c_lflag &= ~ECHO;    // do not echo
    options.c_lflag &= ~ISIG;    // do not generate signals
    options.c_lflag &= ~IEXTEN;  // disable implementation-defined processing

    options.c_iflag &= ~(IXON | IXOFF | IXANY);  // disable XON/XOFF flow control
    options.c_iflag |= IGNBRK;   // ignore BREAK condition
    options.c_iflag |= INPCK;    // enable parity checking
    options.c_iflag |= IGNPAR;   // ignore framing and parity errors
    options.c_iflag &= ~ISTRIP;  // do not strip off 8th bit
    options.c_iflag &= ~INLCR;   // do not translate NL to CR
    options.c_iflag &= ~ICRNL;   // do not translate CR to NL
    options.c_iflag &= ~IGNCR;   // do not ignore CR

    options.c_oflag &= ~OPOST;  // disable implementation-defined processing
    options.c_oflag &= ~ONLCR;  // do not map NL to CR-NL
    options.c_oflag &= ~OCRNL;  // do not map CR to NL
    options.c_oflag &= ~(ONOCR | ONLRET);  // output CR like a normal person
    options.c_oflag &= ~OFILL;  // no fill characters

    // set timeouts
    if (blocking && timeout == 0)
    {
        // wait for at least 1 byte
        options.c_cc[VTIME] = 0;
        options.c_cc[VMIN] = 1;
    }
    else if (blocking) // timeout > 0
    {
        // wait for at least 1 byte or timeout
        options.c_cc[VTIME] = timeout;
        options.c_cc[VMIN] = 0;
    }
    else // !blocking
    {
        // non-blocking
        options.c_cc[VTIME] = 0;
        options.c_cc[VMIN] = 0;
    }

    // set SBUS baud
    options.c_cflag &= ~CBAUD;
    options.c_cflag |= BOTHER;
    options.c_ispeed = options.c_ospeed = CRSF_BAUDRATE;

    if (ioctl(_port, TCSETS2, &options))
    {
        close(_port);
        _port = -1;
        return;
    }
}

CrsfSerial::~CrsfSerial()
{
    if (_port>0) close(_port);
}

// Call from main loop to update
void CrsfSerial::loop()
{
    handleSerialIn();
}

void CrsfSerial::handleSerialIn()
{
    uint8_t b;
    while (read(_port,&b,1)>0)
    {
        _lastReceive = millis();

        if (_passthroughMode)
        {
            if (onShiftyByte)
                onShiftyByte(b);
            continue;
        }

        _rxBuf[_rxBufPos++] = b;
        handleByteReceived();

        if (_rxBufPos == (sizeof(_rxBuf)/sizeof(_rxBuf[0])))
        {
            // Packet buffer filled and no valid packet found, dump the whole thing
            _rxBufPos = 0;
        }
    }

    checkPacketTimeout();
    checkLinkDown();
}

void CrsfSerial::handleByteReceived()
{
    bool reprocess;
    do
    {
        reprocess = false;
        if (_rxBufPos > 1)
        {
            uint8_t len = _rxBuf[1];
            // Sanity check the declared length, can't be shorter than Type, X, CRC
            if (len < 3 || len > CRSF_MAX_PACKET_LEN)
            {
                shiftRxBuffer(1);
                reprocess = true;
            }

            else if (_rxBufPos >= (len + 2))
            {
                uint8_t inCrc = _rxBuf[2 + len - 1];
                uint8_t crc = _crc.calc(&_rxBuf[2], len - 1);
                if (crc == inCrc)
                {
                    processPacketIn(len);
                    shiftRxBuffer(len + 2);
                    reprocess = true;
                }
                else
                {
                    shiftRxBuffer(1);
                    reprocess = true;
                }
            }  // if complete packet
        } // if pos > 1
    } while (reprocess);
}

void CrsfSerial::checkPacketTimeout()
{
    // If we haven't received data in a long time, flush the buffer a byte at a time (to trigger shiftyByte)
    if (_rxBufPos > 0 && millis() - _lastReceive > CRSF_PACKET_TIMEOUT_MS)
        while (_rxBufPos)
            shiftRxBuffer(1);
}

void CrsfSerial::checkLinkDown()
{
    if (_linkIsUp && millis() - _lastChannelsPacket > CRSF_FAILSAFE_STAGE1_MS)
    {
        if (onLinkDown)
            onLinkDown();
        _linkIsUp = false;
    }
}

void CrsfSerial::processPacketIn(uint8_t len)
{
    Q_UNUSED(len)
    const crsf_header_t *hdr = (crsf_header_t *)_rxBuf;
    if (hdr->device_addr == CRSF_ADDRESS_FLIGHT_CONTROLLER)
    {
        switch (hdr->type)
        {
        case CRSF_FRAMETYPE_RC_CHANNELS_PACKED:
            packetChannelsPacked(hdr);
            break;
        case CRSF_FRAMETYPE_LINK_STATISTICS:
            packetLinkStatistics(hdr);
            break;
        }
    } // CRSF_ADDRESS_FLIGHT_CONTROLLER
}

// Shift the bytes in the RxBuf down by cnt bytes
void CrsfSerial::shiftRxBuffer(uint8_t cnt)
{
    // If removing the whole thing, just set pos to 0
    if (cnt >= _rxBufPos)
    {
        _rxBufPos = 0;
        return;
    }

    if (cnt == 1 && onShiftyByte)
        onShiftyByte(_rxBuf[0]);

    // Otherwise do the slow shift down
    uint8_t *src = &_rxBuf[cnt];
    uint8_t *dst = &_rxBuf[0];
    _rxBufPos -= cnt;
    uint8_t left = _rxBufPos;
    while (left--)
        *dst++ = *src++;
}

void CrsfSerial::packetChannelsPacked(const crsf_header_t *p)
{
    crsf_channels_t *ch = (crsf_channels_t *)&p->data;
    _channels[0] = ch->ch0;
    _channels[1] = ch->ch1;
    _channels[2] = ch->ch2;
    _channels[3] = ch->ch3;
    _channels[4] = ch->ch4;
    _channels[5] = ch->ch5;
    _channels[6] = ch->ch6;
    _channels[7] = ch->ch7;
    _channels[8] = ch->ch8;
    _channels[9] = ch->ch9;
    _channels[10] = ch->ch10;
    _channels[11] = ch->ch11;
    _channels[12] = ch->ch12;
    _channels[13] = ch->ch13;
    _channels[14] = ch->ch14;
    _channels[15] = ch->ch15;

    for (unsigned int i=0; i<4; ++i)
        _channels[i] = map(_channels[i], CRSF_CHANNEL_VALUE_MIN, CRSF_CHANNEL_VALUE_MAX, CRSF_CHANNEL_VALUE_1000, CRSF_CHANNEL_VALUE_2000);

    if (!_linkIsUp && onLinkUp)
        onLinkUp();
    _linkIsUp = true;
    _lastChannelsPacket = millis();

    if (onPacketChannels)
        onPacketChannels();

    emit OnPacket();
}

void CrsfSerial::packetLinkStatistics(const crsf_header_t *p)
{
    const crsfLinkStatistics_t *link = (crsfLinkStatistics_t *)p->data;
    memcpy(&_linkStatistics, link, sizeof(_linkStatistics));

    if (onPacketLinkStatistics)
        onPacketLinkStatistics(&_linkStatistics);
}

void CrsfSerial::write(uint8_t b)
{
    ::write(_port,&b,1);
}

void CrsfSerial::write(const uint8_t *buf, size_t len)
{
    ::write(_port,buf,len);
}

void CrsfSerial::queuePacket(uint8_t addr, uint8_t type, const void *payload, uint8_t len)
{
    if (!_linkIsUp)
        return;
    if (_passthroughMode)
        return;
    if (len > CRSF_MAX_PACKET_LEN)
        return;

    uint8_t buf[CRSF_MAX_PACKET_LEN+4];
    buf[0] = addr;
    buf[1] = len + 2; // type + payload + crc
    buf[2] = type;
    memcpy(&buf[3], payload, len);
    buf[len+3] = _crc.calc(&buf[2], len + 1);

    // Busywait until the serial port seems free
    //while (millis() - _lastReceive < 2)
    //    loop();
    write(buf, len + 4);
}

void CrsfSerial::setPassthroughMode(bool val, unsigned int baud)
{
    _passthroughMode = val;
    Q_UNUSED(baud);
}
