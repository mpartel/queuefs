#!/usr/bin/env ruby
#
#   Copyright 2011 Martin Pärtel <martin.partel@gmail.com>
#
#   This file is part of queuefs.
#
#   queuefs is free software: you can redistribute it and/or modify
#   it under the terms of the GNU General Public License as published by
#   the Free Software Foundation, either version 2 of the License, or
#   (at your option) any later version.
#
#   queuefs is distributed in the hope that it will be useful,
#   but WITHOUT ANY WARRANTY; without even the implied warranty of
#   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#   GNU General Public License for more details.
#
#   You should have received a copy of the GNU General Public License
#   along with queuefs.  If not, see <http://www.gnu.org/licenses/>.
#

# TODO: migrate to rspec

require 'common.rb'

include Errno

# Treat parameters as test names and run only those
$only_these_tests = ARGV unless ARGV.empty?

test("simple mount is successful") do
    assert { File.basename(pwd) == TESTDIR_NAME }
end

test("source files are visible") do
    touch('src/file')

    assert { File.exists?('mnt/file') }
end
