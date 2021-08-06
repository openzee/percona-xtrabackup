/*
   Copyright (c) 2003, 2021, Oracle and/or its affiliates.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License, version 2.0,
   as published by the Free Software Foundation.

   This program is also distributed with certain software (including
   but not limited to OpenSSL) that is licensed under separate terms,
   as designated in a particular file or component or in included license
   documentation.  The authors of MySQL hereby grant you an additional
   permission to link the program and your derivative works with the
   separately licensed software that they have included with MySQL.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License, version 2.0, for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301  USA
*/

#ifndef GET_TABLEID_HPP
#define GET_TABLEID_HPP

#include "SignalData.hpp"

#define JAM_FILE_ID 10


/**
 * Convert tabname to table id
 */
class GetTableIdReq {
  /**
   * Sender(s) / Reciver(s)
   */
  // Blocks
  friend class Dbdict;

  friend bool printGET_TABLEID_REQ(FILE *, const Uint32 *, Uint32, Uint16);  
public:
  static constexpr Uint32 SignalLength = 3;
public:
  Uint32 senderData; 
  Uint32 senderRef;
  Uint32 len;
  SECTION( TABLE_NAME = 0 );
};


/**
 * Convert tabname to table id
 */
class GetTableIdRef {
  /**
   * Sender(s) / Reciver(s)
   */
  // Blocks
  friend class Dbdict;

  friend bool printGET_TABLEID_REF(FILE *, const Uint32 *, Uint32, Uint16);  
public:
  static constexpr Uint32 SignalLength = 3;
public:
  Uint32 senderData; 
  Uint32 senderRef;
  Uint32 err;

  enum ErrorCode {
    InvalidTableId  = 709,
    TableNotDefined = 723,
    TableNameTooLong = 702,
    EmptyTable = 1111
  };
};


/**
 * Convert tabname to table id
 */
class GetTableIdConf {
  /**
   * Sender(s) / Reciver(s)
   */
  // Blocks
  friend class Dbdict;

  friend bool printGET_TABLEID_CONF(FILE *, const Uint32 *, Uint32, Uint16);  
public:
  static constexpr Uint32 SignalLength = 4;
public:
  Uint32 senderData; 
  Uint32 senderRef;
  Uint32 tableId;
  Uint32 schemaVersion;
  
};



#undef JAM_FILE_ID

#endif
