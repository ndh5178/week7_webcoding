use strict;
use warnings;

use Cwd qw(abs_path);
use File::Basename qw(dirname);
use File::Path qw(make_path);
use File::Spec;
use IO::Socket::INET;
use IPC::Open3;
use JSON::PP qw(decode_json encode_json);
use Symbol qw(gensym);

my $server_dir = dirname(abs_path(__FILE__));
my $web_root = abs_path(File::Spec->catdir($server_dir, '..'));
my $project_root = abs_path(File::Spec->catdir($web_root, '..'));
my $results_path = File::Spec->catfile($web_root, 'assets', 'results.json');
my $workspace_data_dir = File::Spec->catdir($project_root, 'data');
my $current_csv_path = File::Spec->catfile($workspace_data_dir, 'players_1000000.csv');
my $engine_pid;
my $engine_in;
my $engine_out;
my $engine_err;
my $engine_csv_path;

my %mime_types = (
    html => 'text/html; charset=utf-8',
    css  => 'text/css; charset=utf-8',
    js   => 'application/javascript; charset=utf-8',
    json => 'application/json; charset=utf-8',
    png  => 'image/png',
    jpg  => 'image/jpeg',
    jpeg => 'image/jpeg',
    svg  => 'image/svg+xml',
);

sub executable_path {
    my ($base_path) = @_;
    my @candidates = ($base_path, "$base_path.exe");

    if ($^O eq 'MSWin32') {
        @candidates = ("$base_path.exe", $base_path);
    }

    for my $candidate (@candidates) {
        return $candidate if -f $candidate;
    }

    return $base_path;
}

sub csv_path_for_count {
    my ($count) = @_;
    return File::Spec->catfile($workspace_data_dir, "players_${count}.csv");
}

sub send_response {
    my ($client, $status, $content_type, $body) = @_;
    my $length = length($body);

    print $client "HTTP/1.1 $status\r\n";
    print $client "Content-Type: $content_type\r\n";
    print $client "Content-Length: $length\r\n";
    print $client "Connection: close\r\n";
    print $client "Cache-Control: no-store\r\n";
    print $client "\r\n";
    print $client $body;
}

sub send_json {
    my ($client, $status, $payload) = @_;
    send_response($client, $status, 'application/json; charset=utf-8', encode_json($payload));
}

sub parse_query_string {
    my ($query) = @_;
    my %params;

    return %params if !defined $query || $query eq q{};

    for my $pair (split /&/, $query) {
        my ($key, $value) = split /=/, $pair, 2;
        next if !defined $key;
        $value = '' if !defined $value;
        $key =~ tr/+/ /;
        $value =~ tr/+/ /;
        $key =~ s/%([0-9A-Fa-f]{2})/chr(hex($1))/eg;
        $value =~ s/%([0-9A-Fa-f]{2})/chr(hex($1))/eg;
        $params{$key} = $value;
    }

    return %params;
}

sub run_command {
    my ($program, @args) = @_;
    my $stderr = gensym;
    my $pid;
    my $stdout;
    eval {
        $pid = open3(undef, $stdout, $stderr, $program, @args);
        1;
    } or do {
        my $error = $@ || 'unknown process start error';
        return (1, "failed to start $program: $error");
    };
    my $output = do {
        local $/;
        (<$stdout> // q{}) . (<$stderr> // q{});
    };

    waitpid($pid, 0);
    my $exit_code = $? >> 8;
    return ($exit_code, $output);
}

sub stop_query_engine {
    if ($engine_in) {
        print {$engine_in} "quit\n";
        close $engine_in;
        $engine_in = undef;
    }
    close $engine_out if $engine_out;
    close $engine_err if $engine_err;
    if ($engine_pid) {
        waitpid($engine_pid, 0);
    }

    $engine_pid = undef;
    $engine_out = undef;
    $engine_err = undef;
    $engine_csv_path = undef;
}

sub start_query_engine {
    my ($csv_path) = @_;
    my $query_server = executable_path(File::Spec->catfile($project_root, 'bin', 'query_server'));
    my $stderr = gensym;
    my $pid;
    my $stdout;
    my $stdin;
    my $ready_line;
    my $ready_payload;

    stop_query_engine();

    eval {
        $pid = open3($stdin, $stdout, $stderr, $query_server, $csv_path);
        1;
    } or do {
        my $error = $@ || 'unknown query server start error';
        return (0, "failed to start query server: $error");
    };

    $ready_line = <$stdout>;
    if (!defined $ready_line) {
        local $/;
        my $stderr_text = <$stderr> // q{};
        waitpid($pid, 0);
        return (0, "query server did not start: $stderr_text");
    }

    eval { $ready_payload = decode_json($ready_line); 1 } or do {
        local $/;
        my $stderr_text = <$stderr> // q{};
        waitpid($pid, 0);
        return (0, "query server returned invalid startup payload: $ready_line $stderr_text");
    };

    if (!$ready_payload->{ok} || !$ready_payload->{ready}) {
        local $/;
        my $stderr_text = <$stderr> // q{};
        waitpid($pid, 0);
        return (0, "query server failed to initialize: $stderr_text");
    }

    $engine_pid = $pid;
    $engine_in = $stdin;
    $engine_out = $stdout;
    $engine_err = $stderr;
    $engine_csv_path = $csv_path;
    return (1, undef);
}

sub ensure_query_engine {
    my ($csv_path) = @_;

    if ($engine_pid && $engine_csv_path && $engine_csv_path eq $csv_path) {
        return (1, undef);
    }

    return start_query_engine($csv_path);
}

sub handle_generate {
    my ($client, $query) = @_;
    my %params = parse_query_string($query);
    my $count = $params{count} // 1_000_000;
    my $csv_path;
    my $bench = executable_path(File::Spec->catfile($project_root, 'bin', 'bench'));
    my ($exit_code, $output);

    if ($count !~ /^\d+$/ || $count <= 0) {
        return send_json($client, '400 Bad Request', {
            ok => JSON::PP::false,
            message => 'count must be a positive integer'
        });
    }

    $csv_path = csv_path_for_count($count);
    if (!-f $csv_path) {
        return send_json($client, '404 Not Found', {
            ok => JSON::PP::false,
            message => "prebuilt csv file not found: $csv_path"
        });
    }

    ($exit_code, $output) = run_command($bench, $csv_path, $results_path);

    if ($exit_code != 0) {
        return send_json($client, '500 Internal Server Error', {
            ok => JSON::PP::false,
            message => "failed to benchmark existing csv data: $output",
            error => $output
        });
    }

    $current_csv_path = $csv_path;
    my ($ok, $engine_error) = ensure_query_engine($current_csv_path);
    if (!$ok) {
        return send_json($client, '500 Internal Server Error', {
            ok => JSON::PP::false,
            message => $engine_error
        });
    }

    return send_json($client, '200 OK', {
        ok => JSON::PP::true,
        dataset_size => int($count),
        csv_path => $current_csv_path,
        message => 'Loaded existing CSV data and benchmarked successfully.'
    });
}

sub handle_search {
    my ($client, $query) = @_;
    my %params = parse_query_string($query);
    my $target_id = $params{id};
    my $query_demo = executable_path(File::Spec->catfile($project_root, 'bin', 'query_demo'));
    my ($exit_code, $output);
    my $payload;

    if (!defined $target_id || $target_id !~ /^\d+$/ || $target_id <= 0) {
        return send_json($client, '400 Bad Request', {
            ok => JSON::PP::false,
            message => 'id must be a positive integer'
        });
    }

    if (!-f $current_csv_path) {
        return send_json($client, '400 Bad Request', {
            ok => JSON::PP::false,
            message => 'csv data has not been generated yet'
        });
    }

    my ($ok, $engine_error) = ensure_query_engine($current_csv_path);
    if (!$ok) {
        return send_json($client, '500 Internal Server Error', {
            ok => JSON::PP::false,
            message => $engine_error
        });
    }

    print {$engine_in} "search $target_id\n";
    $output = <$engine_out>;
    if (!defined $output) {
        local $/;
        my $stderr_text = $engine_err ? (<$engine_err> // q{}) : q{};
        stop_query_engine();
        return send_json($client, '500 Internal Server Error', {
            ok => JSON::PP::false,
            message => "query server connection closed unexpectedly: $stderr_text"
        });
    }

    eval { $payload = decode_json($output); 1 } or do {
        return send_json($client, '500 Internal Server Error', {
            ok => JSON::PP::false,
            message => 'search returned invalid JSON',
            error => $output
        });
    };

    return send_json($client, '200 OK', $payload);
}

sub handle_range {
    my ($client, $query) = @_;
    my %params = parse_query_string($query);
    my $lo     = $params{lo} // 1;
    my $hi     = $params{hi} // '';
    my $count  = $params{count} // 1000000;
    my $page   = $params{page} // 1;
    my $page_size = $params{page_size} // 10;
    my $output;
    my $payload;

    unless (
        $lo =~ /^\d+$/
        && $hi =~ /^\d+$/
        && $count =~ /^\d+$/
        && $page =~ /^\d+$/
        && $page_size =~ /^\d+$/
        && $lo > 0
        && $hi >= $lo
        && $count > 0
        && $page > 0
        && $page_size > 0
    ) {
        return send_json($client, '400 Bad Request', {
            ok => JSON::PP::false,
            message => 'lo, hi, count, page, page_size 값이 올바르지 않습니다.'
        });
    }

    my $csv_path = csv_path_for_count($count);
    if (!-f $csv_path) {
        return send_json($client, '404 Not Found', {
            ok => JSON::PP::false,
            message => "prebuilt csv file not found: $csv_path"
        });
    }

    $current_csv_path = $csv_path;
    my ($ok, $engine_error) = ensure_query_engine($current_csv_path);
    if (!$ok) {
        return send_json($client, '500 Internal Server Error', {
            ok => JSON::PP::false,
            message => $engine_error
        });
    }

    print {$engine_in} "range $lo $hi $page $page_size\n";
    $output = <$engine_out>;
    if (!defined $output) {
        local $/;
        my $stderr_text = $engine_err ? (<$engine_err> // q{}) : q{};
        stop_query_engine();
        return send_json($client, '500 Internal Server Error', {
            ok => JSON::PP::false,
            message => "query server connection closed unexpectedly: $stderr_text"
        });
    }

    eval { $payload = decode_json($output); 1 } or do {
        return send_json($client, '500 Internal Server Error', {
            ok => JSON::PP::false,
            message => 'range search returned invalid JSON',
            error => $output
        });
    };

    return send_json($client, '200 OK', $payload);
}

sub handle_top {
    my ($client, $query) = @_;
    my %params = parse_query_string($query);
    my $count  = $params{count} // 1000000;
    my $output;
    my $payload;

    unless ($count =~ /^\d+$/ && $count > 0) {
        return send_json($client, '400 Bad Request', {
            ok => JSON::PP::false,
            message => 'count 값이 올바르지 않습니다.'
        });
    }

    my $csv_path = csv_path_for_count($count);
    if (!-f $csv_path) {
        return send_json($client, '404 Not Found', {
            ok => JSON::PP::false,
            message => "prebuilt csv file not found: $csv_path"
        });
    }

    $current_csv_path = $csv_path;
    my ($ok, $engine_error) = ensure_query_engine($current_csv_path);
    if (!$ok) {
        return send_json($client, '500 Internal Server Error', {
            ok => JSON::PP::false,
            message => $engine_error
        });
    }

    print {$engine_in} "top\n";
    $output = <$engine_out>;
    if (!defined $output) {
        local $/;
        my $stderr_text = $engine_err ? (<$engine_err> // q{}) : q{};
        stop_query_engine();
        return send_json($client, '500 Internal Server Error', {
            ok => JSON::PP::false,
            message => "query server connection closed unexpectedly: $stderr_text"
        });
    }

    eval { $payload = decode_json($output); 1 } or do {
        return send_json($client, '500 Internal Server Error', {
            ok => JSON::PP::false,
            message => 'top ranking returned invalid JSON',
            error => $output
        });
    };

    return send_json($client, '200 OK', $payload);
}

sub serve_static {
    my ($client, $path) = @_;
    my $relative = $path eq '/' ? 'index.html' : substr($path, 1);
    my $file = abs_path(File::Spec->catfile($web_root, split m{/}, $relative));

    if (!$file || index($file, $web_root) != 0 || !-f $file) {
        return send_response($client, '404 Not Found', 'text/plain; charset=utf-8', '404 Not Found');
    }

    open my $fh, '<', $file or return send_response($client, '500 Internal Server Error', 'text/plain; charset=utf-8', 'failed to open file');
    binmode $fh;
    local $/;
    my $body = <$fh>;
    close $fh;

    my ($ext) = $file =~ /\.([^.]+)$/;
    my $content_type = $mime_types{lc($ext // q{})} || 'application/octet-stream';
    return send_response($client, '200 OK', $content_type, $body);
}

my $server = IO::Socket::INET->new(
    LocalAddr => '0.0.0.0',
    LocalPort => 8000,
    Proto     => 'tcp',
    Listen    => 10,
    Reuse     => 1,
) or die "failed to bind server: $!";

print "Serving $web_root at http://0.0.0.0:8000\n";

while (my $client = $server->accept()) {
    my $request_line = <$client>;
    my ($method, $full_path) = split /\s+/, ($request_line // q{});
    my ($path, $query) = split /\?/, ($full_path // '/'), 2;

    while (my $line = <$client>) {
        last if $line =~ /^\s*$/;
    }

    $path =~ s{//+}{/}g;
    $path =~ s{/$}{} if $path ne '/';

    if (!defined $method || $method ne 'GET') {
        send_response($client, '405 Method Not Allowed', 'text/plain; charset=utf-8', 'Method Not Allowed');
    } elsif ($path eq '/api/generate') {
        handle_generate($client, $query);
    } elsif ($path eq '/api/search') {
        handle_search($client, $query);
    } elsif ($path eq '/api/range') {
        handle_range($client, $query);
    } elsif ($path eq '/api/top') {
        handle_top($client, $query);
    } else {
        serve_static($client, $path);
    }

    close $client;
}
