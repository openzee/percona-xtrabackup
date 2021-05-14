#!/bin/bash

# Copyright (c) 2010, 2021, Oracle and/or its affiliates.
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License, version 2.0,
# as published by the Free Software Foundation.
#
# This program is also distributed with certain software (including
# but not limited to OpenSSL) that is licensed under separate terms,
# as designated in a particular file or component or in included license
# documentation.  The authors of MySQL hereby grant you an additional
# permission to link the program and your derivative works with the
# separately licensed software that they have included with MySQL.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License, version 2.0, for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301  USA

if [ "$MYSQL_HOME" = "" ] ; then
  source ../env.properties
  echo MYSQL_HOME=$MYSQL_HOME
  PATH="$MYSQL_LIBEXEC:$MYSQL_BIN:$PATH"
fi

#set -x

#./show_cluster.sh

echo shut down NDB...
./mgm.sh -e shutdown -t 1

timeout=60
echo
echo "waiting ($timeout s) for ndb to shut down..."
"ndb_waiter" -c "$NDB_CONNECT" -t $timeout --no-contact

# need some extra time for ndb_mgmd to terminate
for ((i=0; i<10; i++)) ; do printf "." ; sleep 1 ; done ; echo

#echo
#ps -efa | grep ndb

#set +x
