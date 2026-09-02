#include "rpiInfo.h"
#include <stdio.h>
#include <string.h>
#include <sys/sysinfo.h>
#include <sys/vfs.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <netinet/in.h>
#include <net/if.h>
#include <ifaddrs.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/ioctl.h>
#include <linux/i2c.h>
#include <linux/i2c-dev.h>
#include <fcntl.h>
#include "st7735.h"
#include <stdlib.h>

/*
* Get the IP address of wlan0 or eth0
*/

/* Shown when no interface carries a usable address. */
#define IP_UNAVAILABLE "xxx.xxx.xxx.xxx"

/* Copy src into a buffer of the given size, always terminating. */
static void copy_string(char *buffer, size_t length, const char *src)
{
    if (buffer == NULL || length == 0)
    {
        return;
    }
    strncpy(buffer, src, length - 1);
    buffer[length - 1] = '\0';
}

/*
* Write the IPv4 address of one interface into the caller's buffer.
* Returns 0 on success, -1 if the interface has no address.
*/
static int read_interface_address(const char *interface, char *buffer, size_t length)
{
    int fd;
    struct ifreq ifr;
    const char *address = NULL;

    if (buffer == NULL || length == 0)
    {
        return -1;
    }
    fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0)
    {
        return -1;
    }
    memset(&ifr, 0, sizeof(ifr));
    /* I want to get an IPv4 IP address */
    ifr.ifr_addr.sa_family = AF_INET;
    strncpy(ifr.ifr_name, interface, IFNAMSIZ-1);
    ifr.ifr_name[IFNAMSIZ-1] = '\0';
    if (ioctl(fd, SIOCGIFADDR, &ifr) != 0)
    {
        close(fd);
        return -1;
    }
    close(fd);

    /* inet_ntoa returns a pointer to a static buffer that the next call
       overwrites, so the text is copied out before returning. */
    address = inet_ntoa(((struct sockaddr_in *)&ifr.ifr_addr)->sin_addr);
    if (address == NULL)
    {
        return -1;
    }
    copy_string(buffer, length, address);
    return 0;
}

/*
* Write the address this machine sends from into the caller's buffer.
* Returns 0 on success, -1 if no route could be resolved.
*
* Nothing is sent. A UDP connect() only fixes the peer and has the kernel
* pick a route for it, which is enough for getsockname() to report the
* source address that route chose. The destination therefore has to be
* routable, not reachable, and 192.0.2.1 is from the range RFC 5737 sets
* aside for documentation, so it is matched by the default route and
* belongs to nobody.
*
* This is what makes the lookup independent of interface naming: eth0,
* end0 and enxb827eb000000 all answer the same way. It also steps around
* the docker and hassio bridges that a walk of every interface holding an
* address would otherwise pick up, since those are not where the default
* route points.
*/
static int read_route_source_address(char *buffer, size_t length)
{
    int fd;
    struct sockaddr_in peer;
    struct sockaddr_in local;
    socklen_t local_length = sizeof(local);
    const char *address = NULL;

    if (buffer == NULL || length == 0)
    {
        return -1;
    }
    fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0)
    {
        return -1;
    }
    memset(&peer, 0, sizeof(peer));
    peer.sin_family = AF_INET;
    peer.sin_port = htons(53);
    peer.sin_addr.s_addr = inet_addr("192.0.2.1");
    if (connect(fd, (struct sockaddr *)&peer, sizeof(peer)) != 0)
    {
        close(fd);
        return -1;
    }
    memset(&local, 0, sizeof(local));
    if (getsockname(fd, (struct sockaddr *)&local, &local_length) != 0)
    {
        close(fd);
        return -1;
    }
    close(fd);

    address = inet_ntoa(local.sin_addr);
    if (address == NULL)
    {
        return -1;
    }
    copy_string(buffer, length, address);
    return 0;
}

/*
* Interfaces that hold an address without being how the machine is
* reached. A Home Assistant host runs containers, so it carries the docker
* and hassio bridges and a veth end per container, any of which would be a
* wrong answer as confidently as the right one.
*/
static int is_virtual_interface(const char *name)
{
    static const char *prefixes[] = {
        "docker", "br-", "veth", "hassio", "virbr", "tun", "tap", "vmnet"
    };
    size_t index;

    for (index = 0; index < sizeof(prefixes) / sizeof(prefixes[0]); index++)
    {
        if (strncmp(name, prefixes[index], strlen(prefixes[index])) == 0)
        {
            return 1;
        }
    }
    return 0;
}

/*
* Write the address of the first usable interface into the caller's
* buffer. Returns 0 on success, -1 if none carried one.
*
* Only reached when there is no default route to read an address from, so
* it is picking between interfaces that cannot currently be routed over.
* Wired is preferred over wireless, which is the order the old eth0 then
* wlan0 lookup expressed. Predictable names make every wireless interface
* start with w, and none of the wired ones do.
*/
static int read_any_interface_address(char *buffer, size_t length)
{
    struct ifaddrs *list = NULL;
    struct ifaddrs *entry = NULL;
    char wireless[IP_ADDRESS_LENGTH] = {0};
    const char *address = NULL;
    int found_wireless = 0;
    int found = 0;

    if (buffer == NULL || length == 0)
    {
        return -1;
    }
    if (getifaddrs(&list) != 0)
    {
        return -1;
    }
    for (entry = list; entry != NULL && !found; entry = entry->ifa_next)
    {
        if (entry->ifa_addr == NULL || entry->ifa_addr->sa_family != AF_INET)
        {
            continue;
        }
        if ((entry->ifa_flags & IFF_UP) == 0 || (entry->ifa_flags & IFF_RUNNING) == 0)
        {
            continue;
        }
        if (entry->ifa_flags & IFF_LOOPBACK)
        {
            continue;
        }
        if (is_virtual_interface(entry->ifa_name))
        {
            continue;
        }
        address = inet_ntoa(((struct sockaddr_in *)entry->ifa_addr)->sin_addr);
        if (address == NULL)
        {
            continue;
        }
        if (entry->ifa_name[0] == 'w')
        {
            if (!found_wireless)
            {
                copy_string(wireless, sizeof(wireless), address);
                found_wireless = 1;
            }
            continue;
        }
        copy_string(buffer, length, address);
        found = 1;
    }
    freeifaddrs(list);

    if (found)
    {
        return 0;
    }
    if (found_wireless)
    {
        copy_string(buffer, length, wireless);
        return 0;
    }
    return -1;
}

void get_ip_address(char *buffer, size_t length)
{
    const char *interface = (IPADDRESS_TYPE == WLAN0_ADDRESS) ? "wlan0" : "eth0";

    /* The configured interface wins where it exists, so this keeps meaning
       what IPADDRESS_TYPE says. Where it does not, the name is the only
       thing wrong with the request and falling back answers it anyway
       rather than reporting no address at all. */
    if (read_interface_address(interface, buffer, length) == 0)
    {
        return;
    }
    get_ip_address_new(buffer, length);
}

void get_ip_address_new(char *buffer, size_t length)
{
    /* Asking for eth0 and wlan0 by name is what this used to do, and it
       finds nothing on a host that names its interfaces predictably: a Pi
       running Home Assistant OS presents end0, and the header fell back to
       xxx.xxx.xxx.xxx while the machine had an address the whole time.

       The route the machine actually sends over answers the question
       without naming anything. Walking the interfaces is kept for a
       machine with no default route, where there is no such answer. */
    if (read_route_source_address(buffer, length) == 0)
    {
        return;
    }
    if (read_any_interface_address(buffer, length) == 0)
    {
        return;
    }
    copy_string(buffer, length, IP_UNAVAILABLE);
}



/* Longest "Key:" we look for, plus room for the terminator. */
#define PROC_KEY_MAX 64

typedef struct
{
  const char *key;  /* including the trailing colon, as /proc writes it */
  uint64_t value;   /* filled in when the key is found                  */
  int found;
} ProcField;

/*
* Read "Key: value" lines from a /proc file, filling in every requested
* field in a single pass. Returns the number of fields found, or -1 if the
* file could not be opened.
*
* Scanning stops as soon as every field has been seen, so asking for keys
* that appear near the top of the file costs only those lines.
*/
static int read_proc_fields(const char *path, ProcField *fields, size_t count)
{
  FILE *fp = NULL;
  char line[256];
  char name[PROC_KEY_MAX];
  unsigned long long value = 0;
  size_t index = 0;
  int found = 0;

  for (index = 0; index < count; index++)
  {
    fields[index].value = 0;
    fields[index].found = 0;
  }

  fp = fopen(path, "r");
  if (fp == NULL)
  {
    return -1;
  }

  while (found < (int)count && fgets(line, sizeof(line), fp) != NULL)
  {
    /* The conversion is width-limited. The set of keys we ask for is
       fixed, but the file contents are not, and a bare %s would write
       past `name` on a longer line than we expect. */
    if (sscanf(line, "%63s %llu", name, &value) != 2)
    {
      continue;
    }
    for (index = 0; index < count; index++)
    {
      if (!fields[index].found && strcmp(name, fields[index].key) == 0)
      {
        fields[index].value = (uint64_t)value;
        fields[index].found = 1;
        found++;
        break;
      }
    }
  }

  fclose(fp);
  return found;
}

/*
* get ram memory
*
* Totals are reported in GiB. /proc/meminfo counts kibibytes, despite
* labelling them "kB".
*
* /proc/meminfo is the only source for this: sysinfo(2) has no
* MemAvailable equivalent, and its freeram field is MemFree by another
* name.
*/
void get_cpu_memory(float *Totalram,float *freeram)
{
  ProcField fields[3];

  fields[0].key = "MemTotal:";
  fields[1].key = "MemAvailable:";
  fields[2].key = "MemFree:";

  *Totalram = 0.0;
  *freeram = 0.0;

  if (read_proc_fields("/proc/meminfo", fields, 3) < 0 || !fields[0].found)
  {
    return;
  }

  *Totalram = fields[0].value / (float)(1024 * 1024);
  /* MemAvailable is the kernel's own estimate of what a new workload could
     claim without swapping. MemFree is not a substitute: it excludes the
     reclaimable page cache that Linux deliberately fills with every spare
     page, so a healthy machine reads as nearly full. On a 32G host MemFree
     reports 90% used where MemAvailable reports 50%.

     Kernels older than 3.14 do not publish MemAvailable. */
  if (fields[1].found)
  {
    *freeram = fields[1].value / (float)(1024 * 1024);
  }
  else
  {
    *freeram = fields[2].value / (float)(1024 * 1024);
  }
}

/* Upper bound on the number of distinct filesystems we will total up. */
#define MAX_TRACKED_FS 32

typedef enum
{
  DISK_FILTER_ROOT,     /* only the filesystem mounted on "/"                */
  DISK_FILTER_NON_ROOT, /* every block-device filesystem except that one     */
  DISK_FILTER_ALL       /* every block-device filesystem                     */
} DiskFilter;

/*
* Resolve the device a path lives on, so that two mount entries backed by the
* same filesystem can be recognised as one.
*/
static int get_path_device(const char *path, dev_t *dev)
{
  struct stat st;
  if (stat(path, &st) != 0)
  {
    return -1;
  }
  *dev = st.st_dev;
  return 0;
}

/*
* Total the capacity, used and available bytes of the mounted filesystems
* selected by "filter", and return how many were counted (-1 on failure).
*
* /proc/mounts is walked instead of shelling out to df: no subprocess is
* needed, and because each filesystem is de-duplicated by its device id, a
* filesystem reachable through more than one mount entry is counted once.
* That is what makes DISK_FILTER_ROOT and DISK_FILTER_NON_ROOT disjoint, and
* therefore safe for a caller to add together, no matter whether the system
* boots from an SD card, USB or NVMe.
*
* The three byte counts mirror the columns df prints, so that a percentage
* derived from them agrees with df rather than drifting from it.
*/
static int sum_mounts(DiskFilter filter, uint64_t *totalBytes,
                      uint64_t *usedBytes, uint64_t *availBytes)
{
  FILE *fp = NULL;
  char line[512];
  char device[256];
  char mountPoint[256];
  dev_t seen[MAX_TRACKED_FS];
  int seenCount = 0;
  dev_t rootDev = 0;
  int haveRootDev = 0;

  *totalBytes = 0;
  *usedBytes = 0;
  *availBytes = 0;

  haveRootDev = (get_path_device("/", &rootDev) == 0);
  if (!haveRootDev && filter != DISK_FILTER_ALL)
  {
    /* Without knowing which filesystem is the root one we cannot honour a
       root/non-root split without risking counting it on both sides. */
    return -1;
  }

  fp = fopen("/proc/mounts", "r");
  if (fp == NULL)
  {
    return -1;
  }

  while (fgets(line, sizeof(line), fp) != NULL)
  {
    struct statfs fsInfo;
    uint64_t blockSize = 0;
    dev_t dev = 0;
    int duplicate = 0;
    int index = 0;

    if (sscanf(line, "%255s %255s", device, mountPoint) != 2)
    {
      continue;
    }

    /* Only real block devices hold user data. Loop devices are read-only
       images (snaps, mounted ISOs) that always read as 100% full, so
       including them would skew the total. */
    if (strncmp(device, "/dev/", 5) != 0 ||
        strncmp(device, "/dev/loop", 9) == 0)
    {
      continue;
    }

    if (get_path_device(mountPoint, &dev) != 0)
    {
      continue;
    }

    if (filter != DISK_FILTER_ALL)
    {
      int isRoot = (dev == rootDev);
      if (isRoot != (filter == DISK_FILTER_ROOT))
      {
        continue;
      }
    }

    for (index = 0; index < seenCount; index++)
    {
      if (seen[index] == dev)
      {
        duplicate = 1;
        break;
      }
    }
    if (duplicate)
    {
      continue;
    }
    if (seenCount >= MAX_TRACKED_FS)
    {
      /* Out of room to remember what has already been counted; skipping is
         an undercount, whereas counting on would risk a double count. */
      continue;
    }
    seen[seenCount++] = dev;

    if (statfs(mountPoint, &fsInfo) != 0 || fsInfo.f_blocks == 0)
    {
      continue;
    }

    blockSize = (uint64_t)fsInfo.f_bsize;
    /* "Size", "Used" and "Avail" as df defines them. Used counts the blocks
       reserved for root, which f_bavail excludes, so used + avail is smaller
       than the total; that gap is what df's Use% column divides by. */
    *totalBytes += blockSize * (uint64_t)fsInfo.f_blocks;
    *usedBytes += blockSize * ((uint64_t)fsInfo.f_blocks - (uint64_t)fsInfo.f_bfree);
    *availBytes += blockSize * (uint64_t)fsInfo.f_bavail;
  }

  fclose(fp);
  return seenCount;
}

/*
* Total every mounted block-device filesystem, each counted exactly once.
*/
int get_disk_usage(uint64_t *totalBytes, uint64_t *usedBytes, uint64_t *availBytes)
{
  return sum_mounts(DISK_FILTER_ALL, totalBytes, usedBytes, availBytes);
}

/*
* get sd memory
*
* Reports the filesystem mounted on "/", in whole GiB. Note that despite its
* name "freesize" receives the space in use, which is what callers display.
*/
void get_sd_memory(uint32_t *MemSize, uint32_t *freesize)
{
    uint64_t totalBytes = 0;
    uint64_t usedBytes = 0;
    uint64_t availBytes = 0;

    *MemSize = 0;
    *freesize = 0;

    if (sum_mounts(DISK_FILTER_ROOT, &totalBytes, &usedBytes, &availBytes) <= 0)
    {
      return;
    }
    *MemSize = (uint32_t)(totalBytes >> 30);
    *freesize = (uint32_t)(usedBytes >> 30);
}


/*
* get hard disk memory
*
* Reports every block-device filesystem other than the one mounted on "/",
* in whole GiB, so that it never overlaps with get_sd_memory().
*/
uint8_t get_hard_disk_memory(uint16_t *diskMemSize, uint16_t *useMemSize)
{
  uint64_t totalBytes = 0;
  uint64_t usedBytes = 0;
  uint64_t availBytes = 0;

  *diskMemSize = 0;
  *useMemSize = 0;

  if (sum_mounts(DISK_FILTER_NON_ROOT, &totalBytes, &usedBytes, &availBytes) <= 0)
  {
    return 0;
  }
  *diskMemSize = (uint16_t)(totalBytes >> 30);
  *useMemSize = (uint16_t)(usedBytes >> 30);
  return 1;
}

/*
* get temperature
*/

uint8_t get_temperature(void)
{
    FILE *fd;
    unsigned int temp = 0;
    char buff[10] = {0};
    fd = fopen("/sys/class/thermal/thermal_zone0/temp","r");
    if (fd == NULL)
    {
        return 0;
    }
    if (fgets(buff,sizeof(buff),fd) == NULL || sscanf(buff, "%u", &temp) != 1)
    {
        fclose(fd);
        return 0;
    }
    fclose(fd);
    return TEMPERATURE_TYPE == FAHRENHEIT ? temp/1000*1.8+32 : temp/1000;
}

/* Percentage of busy jiffies out of total, rounded to nearest. */
static uint8_t busy_percent(uint64_t busy, uint64_t total)
{
    if (total == 0 || busy > total)
    {
        return 0;
    }
    return (uint8_t)((busy * 100 + total / 2) / total);
}

/*
* Get cpu usage
*
* Read straight from /proc/stat rather than by running top(1). The counters
* there are cumulative jiffy totals since boot, so the load over an interval
* is the proportion of non-idle jiffies accumulated between two reads. That
* is what top itself computes, without the cost of forking a shell, top,
* grep and awk on every sample, and without depending on top's output
* format, column order or locale.
*
* The previous reading is kept in statics; this is called from a single
* thread.
*/
uint8_t get_cpu_message(void)
{
    static uint64_t prevTotal = 0;
    static uint64_t prevIdle = 0;
    FILE *fp = NULL;
    char line[512];
    unsigned long long field[10] = {0};
    uint64_t total = 0;
    uint64_t idle = 0;
    uint64_t totalDelta = 0;
    uint64_t idleDelta = 0;
    int count = 0;
    int index = 0;

    fp = fopen("/proc/stat", "r");
    if (fp == NULL)
    {
        return 0;
    }
    if (fgets(line, sizeof(line), fp) == NULL)
    {
        fclose(fp);
        return 0;
    }
    fclose(fp);

    /* The aggregate line: "cpu" then user, nice, system, idle, iowait, irq,
       softirq, steal, guest and guest_nice. Older kernels publish fewer of
       them, so however many are present is however many we total. */
    count = sscanf(line, "cpu %llu %llu %llu %llu %llu %llu %llu %llu %llu %llu",
                   &field[0], &field[1], &field[2], &field[3], &field[4],
                   &field[5], &field[6], &field[7], &field[8], &field[9]);
    if (count < 4)
    {
        return 0;
    }
    for (index = 0; index < count; index++)
    {
        total += (uint64_t)field[index];
    }
    /* Time spent waiting on I/O is not the CPU doing work. */
    idle = (uint64_t)field[3] + (count > 4 ? (uint64_t)field[4] : 0);

    if (prevTotal != 0 && total > prevTotal)
    {
        totalDelta = total - prevTotal;
        idleDelta = (idle > prevIdle) ? (idle - prevIdle) : 0;
        prevTotal = total;
        prevIdle = idle;
        if (idleDelta >= totalDelta)
        {
            return 0;
        }
        return busy_percent(totalDelta - idleDelta, totalDelta);
    }

    /* First call, so there is no interval to measure yet: report the
       since-boot average for this one reading, which is what top -n1
       returned anyway. Every later call measures a real interval. */
    prevTotal = total;
    prevIdle = idle;
    return busy_percent(total - idle, total);
}