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
#pragma once

#include <QByteArray>
#include <QHostAddress>

#include <cstdint>

/// Parser for the HAProxy PROXY protocol (v1 text and v2 binary). Used by the server's connection-accept path
/// to recover the real client address when Fulcrum sits behind a trusted L4 reverse proxy (nginx/NPM, HAProxy,
/// etc.) that prepends a PROXY header to each connection.
///
/// See: https://www.haproxy.org/download/1.8/doc/proxy-protocol.txt
namespace ProxyProtocol {

    /// The largest header we are willing to buffer/consume. v1 lines are <= 107 bytes; v2 is 16 bytes + an
    /// address block whose declared length we cap here (to bound memory and reject abuse).
    static constexpr int kMaxHeaderLen = 16 + 1024;

    enum class Status {
        NeedMore,   ///< `buf` is a valid prefix of a (possibly) complete header but is not complete yet -- read more bytes and retry.
        Absent,     ///< `buf` does not begin with a PROXY v1 or v2 signature -- there is no header (treat data as normal protocol bytes).
        Parsed,     ///< A complete, well-formed header was parsed. `Result::consumed` bytes must be discarded from the stream.
        Error,      ///< A header was started (signature matched) but is malformed or over-long -- the connection should be dropped.
    };

    struct Result {
        QHostAddress srcAddr;   ///< the real client source address (only meaningful if `haveAddr`)
        quint16 srcPort = 0;    ///< the real client source port (only meaningful if `haveAddr`)
        int consumed = 0;       ///< number of leading bytes of the stream that constitute the header (to be discarded)
        bool haveAddr = false;  ///< false for v1 "UNKNOWN" and v2 LOCAL/UNSPEC -- header present but carries no usable address
    };

    /// Attempts to parse a PROXY protocol header from the front of `buf` (which should be a peek of the socket's
    /// incoming bytes, up to kMaxHeaderLen). Does not modify the socket. See `Status` for the possible outcomes.
    Status parse(const QByteArray & buf, Result & out);

    /// Cheap best-effort signature check: does `buf` begin with a PROXY protocol v1 ("PROXY") or v2 (binary)
    /// signature? For diagnostics only (e.g. warning when a header arrives on a connection we are not configured
    /// to consume it on). This is NOT a parser -- use parse() to actually validate/consume a header.
    bool looksLikeHeader(const QByteArray & buf);

} // namespace ProxyProtocol
