//
// Fulcrum - A fast & nimble SPV Server for Bitcoin Cash
// Copyright (C) 2019-2026 Calin A. Culianu <calin.culianu@gmail.com>
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program (see LICENSE.txt).  If not, see
// <https://www.gnu.org/licenses/>.
//
#include "ProxyProtocol.h"

#include <QList>

#include <algorithm>
#include <cstring>

namespace {
    // The v2 12-byte binary signature: "\r\n\r\n\0\r\nQUIT\n"
    constexpr char kV2Sig[12] = {'\x0D','\x0A','\x0D','\x0A','\x00','\x0D','\x0A','\x51','\x55','\x49','\x54','\x0A'};
    // The v1 signature is the ASCII "PROXY" followed by a space or (for the abbreviated UNKNOWN form) CRLF.
    constexpr char kV1Sig[5] = {'P','R','O','X','Y'};

    inline unsigned char at(const QByteArray & b, int i) { return static_cast<unsigned char>(b[i]); }

    ProxyProtocol::Status parseV1(const QByteArray & buf, ProxyProtocol::Result & out) {
        // Find the terminating CRLF. The whole header (including CRLF) must be <= 107 bytes.
        const int crlf = buf.indexOf("\r\n");
        if (crlf < 0) {
            // No CRLF yet. If we already have more than the max allowed without a CRLF, it's malformed.
            return buf.size() < 107 ? ProxyProtocol::Status::NeedMore : ProxyProtocol::Status::Error;
        }
        if (crlf + 2 > 107)
            return ProxyProtocol::Status::Error;
        const QByteArray line = buf.left(crlf); // header without CRLF
        out.consumed = crlf + 2;

        const QList<QByteArray> tok = line.split(' ');
        // Expected: "PROXY TCP4 <src> <dst> <sport> <dport>" or "PROXY UNKNOWN ..."
        if (tok.isEmpty() || tok.front() != "PROXY")
            return ProxyProtocol::Status::Error;
        if (tok.size() < 2)
            return ProxyProtocol::Status::Error;
        const QByteArray & proto = tok[1];
        if (proto == "UNKNOWN") {
            // Header present but no usable address. Caller keeps the real (proxy) socket address.
            out.haveAddr = false;
            return ProxyProtocol::Status::Parsed;
        }
        if (proto != "TCP4" && proto != "TCP6")
            return ProxyProtocol::Status::Error;
        if (tok.size() != 6)
            return ProxyProtocol::Status::Error;
        QHostAddress src;
        if (!src.setAddress(QString::fromLatin1(tok[2])))
            return ProxyProtocol::Status::Error;
        // Enforce that the address family matches the declared proto (defense against spoofed/garbled lines).
        const bool isV4 = src.protocol() == QAbstractSocket::IPv4Protocol;
        if ((proto == "TCP4") != isV4)
            return ProxyProtocol::Status::Error;
        bool okPort = false;
        const uint port = tok[4].toUInt(&okPort);
        if (!okPort || port > 65535u)
            return ProxyProtocol::Status::Error;
        out.srcAddr = src;
        out.srcPort = static_cast<quint16>(port);
        out.haveAddr = true;
        return ProxyProtocol::Status::Parsed;
    }

    ProxyProtocol::Status parseV2(const QByteArray & buf, ProxyProtocol::Result & out) {
        // Fixed 16-byte header: 12 sig + 1 ver/cmd + 1 fam/proto + 2 len (big-endian).
        if (buf.size() < 16)
            return ProxyProtocol::Status::NeedMore;
        const unsigned char verCmd = at(buf, 12);
        const unsigned char famProto = at(buf, 13);
        const unsigned len = (static_cast<unsigned>(at(buf, 14)) << 8) | static_cast<unsigned>(at(buf, 15));
        if (len > 1024u) // cap -- reject implausibly large address blocks
            return ProxyProtocol::Status::Error;
        const int total = 16 + static_cast<int>(len);
        if (buf.size() < total)
            return ProxyProtocol::Status::NeedMore;
        out.consumed = total;

        if ((verCmd & 0xF0u) != 0x20u) // high nibble must be protocol version 2
            return ProxyProtocol::Status::Error;
        const unsigned cmd = verCmd & 0x0Fu; // 0 = LOCAL, 1 = PROXY
        if (cmd == 0x0u) {
            // LOCAL (e.g. proxy health check). Header present but no address -- keep real socket address.
            out.haveAddr = false;
            return ProxyProtocol::Status::Parsed;
        }
        if (cmd != 0x1u)
            return ProxyProtocol::Status::Error;

        const unsigned fam = (famProto & 0xF0u) >> 4; // 1 = AF_INET, 2 = AF_INET6
        // low nibble is transport (1 = STREAM); we only care about the family for address extraction.
        if (fam == 0x1u) { // IPv4: src(4) dst(4) sport(2) dport(2) = 12 bytes
            if (len < 12u)
                return ProxyProtocol::Status::Error;
            quint32 v4 = 0;
            for (int i = 0; i < 4; ++i) v4 = (v4 << 8) | at(buf, 16 + i);
            out.srcAddr = QHostAddress(v4);
            out.srcPort = static_cast<quint16>((static_cast<unsigned>(at(buf, 24)) << 8) | at(buf, 25));
            out.haveAddr = true;
            return ProxyProtocol::Status::Parsed;
        } else if (fam == 0x2u) { // IPv6: src(16) dst(16) sport(2) dport(2) = 36 bytes
            if (len < 36u)
                return ProxyProtocol::Status::Error;
            quint8 v6[16];
            for (int i = 0; i < 16; ++i) v6[i] = at(buf, 16 + i);
            out.srcAddr = QHostAddress(v6);
            out.srcPort = static_cast<quint16>((static_cast<unsigned>(at(buf, 48)) << 8) | at(buf, 49));
            out.haveAddr = true;
            return ProxyProtocol::Status::Parsed;
        }
        // AF_UNSPEC (0) or AF_UNIX (3): header present but no usable IP address -- keep real socket address.
        out.haveAddr = false;
        return ProxyProtocol::Status::Parsed;
    }
} // namespace

namespace ProxyProtocol {

Status parse(const QByteArray & buf, Result & out) {
    out = Result{};
    const int n = buf.size();
    if (n == 0)
        return Status::NeedMore;

    // Could this be a v2 binary header? Compare against as much of the 12-byte signature as we have.
    {
        const int cmp = std::min(n, 12);
        if (std::memcmp(buf.constData(), kV2Sig, static_cast<size_t>(cmp)) == 0) {
            if (n < 16)
                return Status::NeedMore; // matches the signature so far; need the full fixed header
            return parseV2(buf, out);
        }
    }
    // Could this be a v1 text header? It must begin with "PROXY".
    {
        const int cmp = std::min(n, 5);
        if (std::memcmp(buf.constData(), kV1Sig, static_cast<size_t>(cmp)) == 0) {
            if (n < 6)
                return Status::NeedMore;
            // The byte after "PROXY" must be a space per the spec.
            if (at(buf, 5) != ' ')
                return Status::Error;
            return parseV1(buf, out);
        }
    }
    // Neither signature -- there is no PROXY header on this connection.
    return Status::Absent;
}

bool looksLikeHeader(const QByteArray & buf) {
    // Best-effort signature check for diagnostics only (see header). v1 = ASCII "PROXY"; v2 = 12-byte binary sig.
    if (buf.size() >= int(sizeof kV1Sig) && std::memcmp(buf.constData(), kV1Sig, sizeof kV1Sig) == 0)
        return true;
    if (buf.size() >= int(sizeof kV2Sig) && std::memcmp(buf.constData(), kV2Sig, sizeof kV2Sig) == 0)
        return true;
    return false;
}

} // namespace ProxyProtocol
