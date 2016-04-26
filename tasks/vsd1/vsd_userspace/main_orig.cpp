/*
 * TODO parse command line arguments and call proper
 * VSD_IOCTL_* using C function ioctl (see man ioctl).
 */
#include <fcntl.h> // open
#include <bsd/stdlib.h> // getprogname
#include <sys/ioctl.h> // ioctl
#include <vsd_ioctl.h> // ioctl interface for driver

#include <iostream>
#include <map>
#include <sstream>

using namespace std;

static const char* DEVICE_FILE_NAME = "/dev/vsd";
static const string executable_name = getprogname();
static const string advice = "Type \"" + executable_name + " help\" to see manual.\n";

enum Command
{
    UNKNOWN,
    SET_SIZE,
    GET_SIZE,
    HELP
};

static map<string, Command> command_to_code = { {"size_set", SET_SIZE}, 
    {"size_get", GET_SIZE}, {"help", HELP} };

void print_help(const string &exec_name)
{
    cout << "USAGE: " << exec_name << " <command>" << endl;
    cout << "COMMANDS:\n";
    cout << "\tsize_get\n";
    cout << "\t\tGet current vsd size\n";
    cout << "\tsize_set SIZE\n";
    cout << "\t\tSet vsd size to SIZE bytes\n";
    cout << "\thelp\n";
    cout << "\t\tPrint this help\n";
}

int get_size(vsd_ioctl_set_size_arg_t &size_arg)
{
    int fd = open(DEVICE_FILE_NAME, 0);
    return ioctl(fd, VSD_IOCTL_GET_SIZE, &size_arg);
}

int set_size(const vsd_ioctl_set_size_arg_t &size_arg)
{
    int fd = open(DEVICE_FILE_NAME, 0);
    return ioctl(fd, VSD_IOCTL_SET_SIZE, &size_arg);
}

int process_get_request()
{
    vsd_ioctl_set_size_arg_t size_arg;
    int ret = get_size(size_arg);
    if (ret == 0) {
        cout << "VSD size is " << size_arg.size << " bytes.\n";
    } else {
        cout << "Error reading VSD size.\n";
    }

    return ret;
}

int process_set_request(int argc, char **argv)
{
    vsd_ioctl_set_size_arg_t size_arg;
    if (argc == 2) {
        cout << "Size not provided for size_set command. " << advice;
        return 1;
    }
    istringstream ss(argv[2]);
    ss >> size_arg.size;
    if (ss.fail() || !ss.eof()) {
        cout << "Wrong size format. Should be integer decimal value.\n";
        return 1;
    }

    int ret = set_size(size_arg);
    if (ret == 0) {
        cout << "VSD size set to " << size_arg.size << " bytes.\n"; 
    }
    else {
        cout << "Error setting VSD size.\n";
    }

    return ret;
}

int main(int argc, char **argv) {
    if (argc < 2 || argc > 3) {
        cout << "Incorrect number of arguments! " << advice;
        return EXIT_FAILURE;
    }

    int ret = 0;
    string command = argv[1];
    switch (command_to_code[command])
    {
        case HELP:
            print_help(executable_name);
            break;
        case GET_SIZE:
        {
            ret = process_get_request();
            break;
        }
        case SET_SIZE:
        {
            ret = process_set_request(argc, argv);
            break;
        }
        default:
            cout << "Unknown command. " << advice;
            ret = 1;
    }
    return (ret == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
