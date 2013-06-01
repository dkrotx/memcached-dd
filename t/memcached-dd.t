#!/usr/bin/perl

## memcached-dd test:
# Check what we dump the same values we passed.
# Also check what expired items are no dumped.

use strict;
use Test::More tests => 16;
use FindBin qw($Bin);
use lib "$Bin/lib";
use File::Temp qw/ tempdir /;
use MemcachedTest;

my $tempdir = tempdir("memcached-dd.t.XXXXXX", CLEANUP => 1);
my $pidfile = "$tempdir/memcached.pid";
my $dumpfile = "$tempdir/dump1";

sub save_dump {
    open(my $pidf, "<", $pidfile) or die "Failed to open $pidfile: $!\n";
    my $pid = <$pidf>;
    close $pidf;

    ok(! -e $dumpfile, "dumpfile not exists before dump");

    kill('USR2', $pid);
    for(1..20) {
        last if (-e $dumpfile);
        sleep(0.1);
    }

    ok(-e $dumpfile, "dumpfile written");
}

my $server = new_memcached("-F $dumpfile -P $pidfile");
my $sock = $server->sock;
my $exptime;

# set keys in delta- and timestamp-mode

# key0,key1 - should be expired when dumped
print $sock "set key1 0 2 4\r\nval1\r\n";
is(scalar <$sock>, "STORED\r\n", "stored key1");

$exptime = time() + 2; # the same as key1
print $sock "set key2 0 $exptime 4\r\nval2\r\n";
is(scalar <$sock>, "STORED\r\n", "stored key2");

print $sock "set key3 0 120 4\r\nval3\r\n";
is(scalar <$sock>, "STORED\r\n", "stored key3");

$exptime = time() + 120; # the same as key3
print $sock "set key4 0 $exptime 4\r\nval4\r\n";
is(scalar <$sock>, "STORED\r\n", "stored key4");

print $sock "set key5 0 120 4\r\nval5\r\n";
is(scalar <$sock>, "STORED\r\n", "stored key5");

$exptime = time() + 120; # the same as key5
print $sock "set key6 0 $exptime 4\r\nval6\r\n";
is(scalar <$sock>, "STORED\r\n", "stored key6");

# huge timestamp, 1 year in future
# issue found by Andrew Ivanov <bergshrund@gmail.com>
$exptime = time() + 60*60*24*365;
print $sock "set key7 0 $exptime 4\r\nval7\r\n";
is(scalar <$sock>, "STORED\r\n", "stored key7");

sleep(3); # waiting for 2-second keys will be expired

save_dump();

## now restore dump with new instance of memcached
$server = new_memcached("-F $dumpfile");
$sock = $server->sock;

mem_get_is($sock, "key1", undef, "should be expired before dump");
mem_get_is($sock, "key2", undef, "should be expired before dump");

mem_get_is($sock, "key3", "val3");
mem_get_is($sock, "key4", "val4");
mem_get_is($sock, "key5", "val5");
mem_get_is($sock, "key6", "val6");
mem_get_is($sock, "key7", "val7");
