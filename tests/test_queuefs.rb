#!/usr/bin/env ruby                                                          
# Copyright (c) 2011 Martin Pärtel <martin.partel@gmail.com>
# 
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
# 
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.
# 
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
# THE SOFTWARE.
# 


require 'common.rb'

include Errno

def logfile
    IO.readlines 'logfile'
end

def logfile_contains(expected_line)
    logfile.any? {|line| line.strip == expected_line }
end

def flush_jobs
    system('sync')
    got_signal = false
    Signal.trap("SIGUSR2") do
        got_signal = true
    end
    
    begin
        Process.kill("SIGUSR2", $queuefs_daemon_pid)
        while !got_signal
            sleep 0.001
        end
    ensure
        Signal.trap("SIGUSR2", "DEFAULT") 
    end
end


test "simple mount is successful" do
    assert { File.basename(pwd) == TESTDIR_NAME }
end

test "source files are visible" do
    touch('src/file')
    assert { File.exists?('mnt/file') }
end

test "writing a file makes a job" do
    touch('mnt/file')
    flush_jobs
    assert { logfile_contains 'src/file' }
end

# FIXME: -r 300 should not be necessary
test "retry failed job", :options => '-r 300', :cmd => 'test -f {}2 && echo {} >> logfile' do
    touch('mnt/file')
    flush_jobs
    assert { !logfile_contains 'src/file' }
    touch('src/file2')
    flush_jobs
    assert { logfile_contains 'src/file' }
end
