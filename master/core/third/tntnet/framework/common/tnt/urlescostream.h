/*
 * Copyright (C) 2003-2005 Tommi Maekitalo
 * 
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 * 
 * As a special exception, you may use this file as part of a free
 * software library without restriction. Specifically, if other files
 * instantiate templates or use macros or inline functions from this
 * file, or you compile this file and link it with other files to
 * produce an executable, this file does not by itself cause the
 * resulting executable to be covered by the GNU General Public
 * License. This exception does not however invalidate any other
 * reasons why the executable file might be covered by the GNU Library
 * General Public License.
 * 
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 * 
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */


#ifndef TNT_URLESCOSTREAM_H
#define TNT_URLESCOSTREAM_H

#include <iostream>

namespace tnt
{
  class UrlEscStreamBuf : public std::streambuf
  {
      std::streambuf* sink;

      std::streambuf::int_type overflow(std::streambuf::int_type ch);
      std::streambuf::int_type underflow();
      int sync();

    public:
      UrlEscStreamBuf(std::streambuf* sink_)
        : sink(sink_)
        { }

      void setSink(std::streambuf* sink_)
        { sink = sink_; }
  };

  class UrlEscOstream : public std::ostream
  {
      UrlEscStreamBuf streambuf;

    public:
      UrlEscOstream(std::ostream& sink)
        : std::ostream(0),
          streambuf(sink.rdbuf())
        { init(&streambuf); }
      UrlEscOstream(std::streambuf* sink)
        : std::ostream(0),
          streambuf(sink)
        { init(&streambuf); }

      void setSink(std::ostream& sink)
        { streambuf.setSink(sink.rdbuf()); }
      void setSink(std::streambuf* sink)
        { streambuf.setSink(sink); }
  };

  std::string urlEscape(const std::string& str);
}

#endif // TNT_URLESCOSTREAM_H

