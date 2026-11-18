/*
 * mapload: how fast device memory can be mapped while other mappings of
 * the same kind stay in place.
 *
 * Every mmap(2) of a character device that uses the device pager goes
 * through cdev_pager_allocate(), which looks the device's handle up among
 * all device-pager objects in the system (dev_pager_object_list). With
 * LinuxKPI drivers (drm-kmod), each fault on the mapping also looks the
 * mapping up among all LinuxKPI mappings (linux_vma_head). A GL program
 * keeps thousands of buffer objects mapped, so both lookups get longer
 * with every buffer it holds.
 *
 * mapload maps HOLD buffers and keeps them mapped, as a GL program keeps
 * its textures, then runs WRITERS threads that map, touch and unmap one
 * more buffer in a loop, and MIXERS threads that do the same with
 * anonymous memory. It prints the running totals once a second; device
 * maps per second is the figure of merit.
 *
 * DRM mode (default, -d /dev/dri/cardN): 64x64x32 dumb buffers (16 KB),
 * through DRM_IOCTL_MODE_CREATE_DUMB/MAP_DUMB; any KMS driver with dumb
 * buffers works. Needs access to the card (group video).
 *
 * Sound mode (-S /dev/dspN): no GPU needed. Each mmap(2) of a sound
 * device's output buffer (dsp_mmap_single() in sys/dev/sound/pcm/dsp.c)
 * also makes a new device-pager object. One page is mapped; nothing is
 * played. -H 2000 is a good size.
 *
 *	cc -O2 -Wall -o mapload mapload.c -lpthread
 *	./mapload [-w writers] [-m mixers] [-H hold] [-t seconds]
 *	    [-d /dev/dri/cardN | -S /dev/dspN]
 *
 * Defaults: 3 writers, 3 mixers, 6000 held buffers, 60 seconds,
 * /dev/dri/card0.
 */
#include <sys/types.h>
#include <sys/ioccom.h>
#include <sys/soundcard.h>
#include <sys/mman.h>

#include <err.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

/* From libdrm's drm_mode.h, so that no libdrm headers are needed. */
struct drm_mode_create_dumb {
	uint32_t height, width, bpp, flags;
	uint32_t handle, pitch;
	uint64_t size;
};
struct drm_mode_map_dumb {
	uint32_t handle, pad;
	uint64_t offset;
};
struct drm_mode_destroy_dumb {
	uint32_t handle;
};
#define	DRM_IOCTL_MODE_CREATE_DUMB  _IOWR('d', 0xB2, struct drm_mode_create_dumb)
#define	DRM_IOCTL_MODE_MAP_DUMB     _IOWR('d', 0xB3, struct drm_mode_map_dumb)
#define	DRM_IOCTL_MODE_DESTROY_DUMB _IOWR('d', 0xB4, struct drm_mode_destroy_dumb)

#define	BUF_W		64
#define	BUF_H		64
#define	BUF_BPP		32
#define	ANON_SIZE	65536
#define	MAX_THREADS	64

/* A mapped dumb buffer. */
struct buf {
	uint32_t handle;
	size_t size;
	volatile char *p;
};

static const char *card = "/dev/dri/card0";
static const char *dsp;		/* sound mode: the dsp device */
static size_t dsp_len;		/* bytes of its output buffer mapped */
static atomic_long device_maps, anon_maps, errors;
static volatile sig_atomic_t stop;

static void
on_signal(int sig)
{
	(void)sig;
	stop = 1;
}

/* Create, map and touch one dumb buffer on fd; 0 on success. */
static int
buf_make(int fd, struct buf *b)
{
	struct drm_mode_create_dumb c = { .height = BUF_H, .width = BUF_W,
	    .bpp = BUF_BPP };
	struct drm_mode_map_dumb m = { 0 };
	struct drm_mode_destroy_dumb d;

	if (ioctl(fd, DRM_IOCTL_MODE_CREATE_DUMB, &c) != 0)
		return (-1);
	m.handle = c.handle;
	if (ioctl(fd, DRM_IOCTL_MODE_MAP_DUMB, &m) != 0)
		goto fail;
	b->p = mmap(NULL, c.size, PROT_READ | PROT_WRITE, MAP_SHARED, fd,
	    m.offset);
	if (b->p == MAP_FAILED)
		goto fail;
	b->p[0] = 1;		/* fault a page in through the pager */
	b->handle = c.handle;
	b->size = c.size;
	return (0);
fail:
	d.handle = c.handle;
	ioctl(fd, DRM_IOCTL_MODE_DESTROY_DUMB, &d);
	return (-1);
}

static void
buf_free(int fd, struct buf *b)
{
	struct drm_mode_destroy_dumb d = { .handle = b->handle };

	munmap((void *)b->p, b->size);
	ioctl(fd, DRM_IOCTL_MODE_DESTROY_DUMB, &d);
}

/* Open the sound device for output; its buffer is what gets mapped. */
static int
dsp_open(void)
{
	int fd = open(dsp, O_WRONLY);

	if (fd < 0)
		err(1, "%s", dsp);
	return (fd);
}

/* Map (and touch) the output buffer of fd: one new device object. */
static volatile char *
dsp_map(int fd)
{
	volatile char *p = mmap(NULL, dsp_len, PROT_WRITE, MAP_SHARED, fd, 0);

	if (p == MAP_FAILED)
		return (NULL);
	p[0] = 0;
	return (p);
}

/* New device-pager objects from a sound buffer, as fast as possible. */
static void *
dsp_writer(void *arg)
{
	volatile char *p;
	int fd = dsp_open();

	(void)arg;
	while (!stop) {
		if ((p = dsp_map(fd)) == NULL) {
			errors++;
			continue;
		}
		munmap((void *)p, dsp_len);
		device_maps++;
	}
	close(fd);
	return (NULL);
}

/* New device-pager objects, as fast as possible. */
static void *
writer(void *arg)
{
	struct buf b;
	int fd;

	(void)arg;
	if ((fd = open(card, O_RDWR)) < 0)
		err(1, "%s", card);
	while (!stop) {
		if (buf_make(fd, &b) != 0) {
			errors++;
			continue;
		}
		buf_free(fd, &b);
		device_maps++;
	}
	close(fd);
	return (NULL);
}

/* Anonymous memory churn alongside, as on a busy desktop. */
static void *
mixer(void *arg)
{
	volatile char *p;

	(void)arg;
	while (!stop) {
		p = mmap(NULL, ANON_SIZE, PROT_READ | PROT_WRITE,
		    MAP_ANON | MAP_PRIVATE, -1, 0);
		if (p == MAP_FAILED) {
			errors++;
			continue;
		}
		p[0] = 1;
		p[ANON_SIZE / 2] = 1;
		munmap((void *)p, ANON_SIZE);
		anon_maps++;
	}
	return (NULL);
}

static void
usage(void)
{
	fprintf(stderr, "usage: mapload [-w writers] [-m mixers] [-H hold] "
	    "[-t seconds]\n               [-d /dev/dri/cardN | -S /dev/dspN]\n");
	exit(2);
}

static int
count(const char *s, int max)
{
	char *end;
	long v = strtol(s, &end, 10);

	if (*s == '\0' || *end != '\0' || v < 0 || v > max)
		usage();
	return ((int)v);
}

int
main(int argc, char **argv)
{
	int nwriters = 3, nmixers = 3, nhold = 6000, secs = 60;
	pthread_t threads[MAX_THREADS];
	struct buf *held;
	int ch, fd, n = 0;

	while ((ch = getopt(argc, argv, "w:m:H:t:d:S:")) != -1) {
		switch (ch) {
		case 'w': nwriters = count(optarg, MAX_THREADS); break;
		case 'm': nmixers = count(optarg, MAX_THREADS); break;
		case 'H': nhold = count(optarg, 1000000); break;
		case 't': secs = count(optarg, 86400); break;
		case 'd': card = optarg; break;
		case 'S': dsp = optarg; break;
		default: usage();
		}
	}
	if (optind != argc || nwriters + nmixers > MAX_THREADS)
		usage();

	signal(SIGINT, on_signal);
	signal(SIGTERM, on_signal);

	if ((held = calloc(nhold > 0 ? nhold : 1, sizeof(*held))) == NULL)
		err(1, "calloc");
	if (dsp != NULL) {
		audio_buf_info info;

		fd = dsp_open();
		if (ioctl(fd, SNDCTL_DSP_GETOSPACE, &info) != 0)
			err(1, "%s: SNDCTL_DSP_GETOSPACE", dsp);
		/* One page is enough for a new object; stay inside the buffer. */
		dsp_len = (size_t)getpagesize();
		if ((size_t)info.fragsize * info.fragstotal < dsp_len)
			errx(1, "%s: output buffer smaller than a page", dsp);
		for (int i = 0; i < nhold; i++)
			if ((held[i].p = dsp_map(fd)) == NULL)
				err(1, "held mapping %d of %d", i + 1, nhold);
	} else {
		if ((fd = open(card, O_RDWR)) < 0)
			err(1, "%s", card);
		for (int i = 0; i < nhold; i++)
			if (buf_make(fd, &held[i]) != 0)
				err(1, "held buffer %d of %d", i + 1, nhold);
	}
	printf("%s: holding %d mappings; %d writers, %d mixers, %d s\n",
	    dsp != NULL ? dsp : card, nhold, nwriters, nmixers, secs);
	fflush(stdout);

	for (int i = 0; i < nwriters; i++)
		pthread_create(&threads[n++], NULL,
		    dsp != NULL ? dsp_writer : writer, NULL);
	for (int i = 0; i < nmixers; i++)
		pthread_create(&threads[n++], NULL, mixer, NULL);

	for (int s = 1; s <= secs && !stop; s++) {
		sleep(1);
		printf("%3d s: %ld device maps, %ld anonymous maps, %ld errors\n",
		    s, (long)device_maps, (long)anon_maps, (long)errors);
		fflush(stdout);
	}
	stop = 1;
	for (int i = 0; i < n; i++)
		pthread_join(threads[i], NULL);
	for (int i = 0; i < nhold; i++) {
		if (dsp != NULL)
			munmap((void *)held[i].p, dsp_len);
		else
			buf_free(fd, &held[i]);
	}
	free(held);
	close(fd);
	printf("finished\n");
	return (0);
}
