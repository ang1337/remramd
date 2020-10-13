import subprocess, os, re, sys

def copy_misc_stuff(jail_path):
    os.system("cp --parents /etc/resolv.conf {}".format(jail_path))
    os.system("mkdir -p {}/dev".format(jail_path))
    os.system("mknod {}/dev/null c 1 2".format(jail_path))
    os.system("mknod {}/dev/tty c 1 3".format(jail_path))
    os.system("chmod 666 {}/dev/null {}/dev/tty".format(jail_path, jail_path))

def copy_to_chroot_jail(so_dependencies, cmd_line_args):
    cp_so_deps_cmd = "cp --parents "
    jail_path = cmd_line_args[1]
    for so in so_dependencies:
        cp_so_deps_cmd += (so + " ")

    cp_so_deps_cmd += jail_path
    stripped_usr_dir_cmd = cp_so_deps_cmd.replace("/usr", "") + " " + jail_path 
    os.system(cp_so_deps_cmd)
    os.system(stripped_usr_dir_cmd)
    for arg_idx in range(2, len(cmd_line_args)):
        os.system("cp --parents {} {}".format(cmd_line_args[arg_idx], jail_path))
    if len(jail_path) > 0:
        os.system("rm -rf {}/home".format(jail_path))

def parse_ldd_output(bin_path):
    output = subprocess.check_output(['ldd', bin_path])
    lines = output.splitlines()
    filtered_splitted_lines = []

    for x in lines:
        curr_line = x.decode()
        curr_line = curr_line.strip()
        if re.match(r'.*(/lib/.*|/usr/lib/.*|/lib64/.*|/usr/lib64/.*).+', curr_line) is not None:
            splitted_curr_line = curr_line.split()
            filtered_splitted_lines.append(splitted_curr_line)

    libs = []

    for token in filtered_splitted_lines:
        for string in token:
            if re.match(r'^(/lib/.*|/usr/lib/.*|/lib64/.*|/usr/lib64/.*).+', string) is not None:
                libs.append(string)
    return libs

def print_usage(script_name):
    print("Usage: python {} <path to chroot jail> <list of exposed binaries>".format(script_name))
    sys.exit(1)

if __name__ == "__main__":
    if len(sys.argv) < 2 or not os.path.isdir(sys.argv[1]):
        print_usage(sys.argv[0])

    for arg_idx in range(2, len(sys.argv)):
        if os.path.isdir(sys.argv[arg_idx]):
            print_usage(sys.argv[0])

    for bin_idx in range(2, len(sys.argv)):
        so_dependencies = parse_ldd_output(sys.argv[bin_idx])
        copy_to_chroot_jail(so_dependencies, sys.argv)

    copy_misc_stuff(sys.argv[1])
