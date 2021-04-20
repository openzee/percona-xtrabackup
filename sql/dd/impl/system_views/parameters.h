/* Copyright (c) 2017, 2021, Oracle and/or its affiliates.

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
   Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301  USA */

#ifndef DD_SYSTEM_VIEWS__PARAMETERS_INCLUDED
#define DD_SYSTEM_VIEWS__PARAMETERS_INCLUDED

#include "sql/dd/impl/system_views/system_view_definition_impl.h"
#include "sql/dd/impl/system_views/system_view_impl.h"
#include "sql/dd/string_type.h"

namespace dd {
namespace system_views {

/*
  The class representing INFORMATION_SCHEMA.PARAMETERS system view definition.
*/
class Parameters : public System_view_impl<System_view_select_definition_impl> {
 public:
  enum enum_fields {
    FIELD_SPECIFIC_CATALOG,
    FIELD_SPECIFIC_SCHEMA,
    FIELD_SPECIFIC_NAME,
    FIELD_ORDINAL_POSITION,
    FIELD_PARAMETER_MODE,
    FIELD_PARAMETER_NAME,
    FIELD_DATA_TYPE,
    FIELD_CHARACTER_MAXIMUM_LENGTH,
    FIELD_CHARACTER_OCTET_LENGTH,
    FIELD_NUMERIC_PRECISION,
    FIELD_NUMERIC_SCALE,
    FIELD_DATETIME_PRECISION,
    FIELD_CHARACTER_SET_NAME,
    FIELD_COLLATION_NAME,
    FIELD_DTD_IDENTIFIER,
    FIELD_ROUTINE_TYPE
  };

  Parameters();

  static const Parameters &instance();

  static const String_type &view_name() {
    static String_type s_view_name("PARAMETERS");
    return s_view_name;
  }

  const String_type &name() const override { return Parameters::view_name(); }
};

}  // namespace system_views
}  // namespace dd

#endif  // DD_SYSTEM_VIEWS__PARAMETERS_INCLUDED
