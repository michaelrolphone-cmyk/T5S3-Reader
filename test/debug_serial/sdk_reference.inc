// Exact functions from Arduino ESP32 2.0.17 (LGPL-2.1-or-later).
// Copyright Arduino / Espressif contributors; see upstream source URLs in README.md.

/*
 Stream.cpp - adds parsing methods to Stream class
 Copyright (c) 2008 David A. Mellis.  All right reserved.

 This library is free software; you can redistribute it and/or
 modify it under the terms of the GNU Lesser General Public
 License as published by the Free Software Foundation; either
 version 2.1 of the License, or (at your option) any later version.

 This library is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 Lesser General Public License for more details.

 You should have received a copy of the GNU Lesser General Public
 License along with this library; if not, write to the Free Software
 Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA

 Created July 2011
 parsing functions based on TextFinder library by Michael Margolis
 */

int Stream::timedRead()
{
    int c;
    _startMillis = millis();
    do {
        c = read();
        if(c >= 0) {
            return c;
        }
    } while(millis() - _startMillis < _timeout);
    return -1;     // -1 indicates timeout
}

String Stream::readStringUntil(char terminator)
{
    String ret;
    int c = timedRead();
    while(c >= 0 && c != terminator) {
        ret += (char) c;
        c = timedRead();
    }
    return ret;
}

/*
 WString.cpp - String library for Wiring & Arduino
 ...mostly rewritten by Paul Stoffregen...
 Copyright (c) 2009-10 Hernando Barragan.  All rights reserved.
 Copyright 2011, Paul Stoffregen, paul@pjrc.com
 Modified by Ivan Grokhotkov, 2014 - esp8266 support
 Modified by Michael C. Miller, 2015 - esp8266 progmem support

 This library is free software; you can redistribute it and/or
 modify it under the terms of the GNU Lesser General Public
 License as published by the Free Software Foundation; either
 version 2.1 of the License, or (at your option) any later version.

 This library is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 Lesser General Public License for more details.

 You should have received a copy of the GNU Lesser General Public
 License along with this library; if not, write to the Free Software
 Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

bool String::equals(const char *cstr) const {
    if(len() == 0)
        return (cstr == NULL || *cstr == 0);
    if(cstr == NULL)
        return buffer()[0] == 0;
    return strcmp(buffer(), cstr) == 0;
}

void String::trim(void) {
    if(!buffer() || len() == 0)
        return;
    char *begin = wbuffer();
    while(isspace(*begin))
        begin++;
    char *end = wbuffer() + len() - 1;
    while(isspace(*end) && end >= begin)
        end--;
    unsigned int newlen = end + 1 - begin;
    if(begin > buffer())
        memmove(wbuffer(), begin, newlen);
    setLen(newlen);
    wbuffer()[newlen] = 0;
}

String String::substring(unsigned int left, unsigned int right) const {
    if(left > right) {
        unsigned int temp = right;
        right = left;
        left = temp;
    }
    String out;
    if(left >= len())
        return out;
    if(right > len())
        right = len();
    out.copy(buffer() + left, right - left);
    return out;
}

int HWCDC::available(void)
{
    if(rx_queue == NULL){
        return -1;
    }
    return uxQueueMessagesWaiting(rx_queue);
}

int HWCDC::read(void)
{
    if(rx_queue == NULL){
        return -1;
    }
    uint8_t c = 0;
    if(xQueueReceive(rx_queue, &c, 0)) {
        return c;
    }
    return -1;
}
