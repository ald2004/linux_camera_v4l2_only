#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <sys/mman.h>
#include <linux/videodev2.h>
#include <string.h>
#include <errno.h>
#include <assert.h>

#define DEVICE_FILE "/dev/video0"

unsigned int n_buffers;
typedef struct buffer
{
	void* start;
	size_t  length;
}buffer;
buffer* buffers;

static int xioctl(int fh, int request, void* arg)
{
    int r;
    do
    {
        r = ioctl(fh, request, arg);
    } while (-1 == r && EINTR == errno);

    return r;
}



int getCamFrame(int fd,FILE* file)
{
    struct v4l2_buffer buf;
    memset(&buf, 0, sizeof(buf));

    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;

    if (xioctl(fd, VIDIOC_DQBUF, &buf) == -1)
    {
        if (errno == EAGAIN || errno == EINTR)
        {
            return 0;
        }
        else
        {
            fprintf(stderr, "set VIDIOC_DQBUF failed: %d, %s\n", errno, strerror(errno));
            return -1;
        }
    }

    if (buf.index < n_buffers)
    {
        fwrite((uint8_t*)buffers[buf.index].start, 1, buf.bytesused, file);
        if (-1 == xioctl(fd, VIDIOC_QBUF, &buf))
        {
            fprintf(stderr, "set VIDIOC_QBUF failed: %d, %s\n", errno, strerror(errno));
            return -1;
        }
        
    }
    return 0;
}





void start()
{
    int CameraFd,CameraFps,io_method;
    int ForceFormat = 1;
    int CamHeight = 960;
    int CamWidth = 1280;
    //CameraFd = getcamfd(DEVICE_FILE);
    struct stat st;
    if (stat(DEVICE_FILE, &st) == -1) {
        fprintf(stderr, "Cannot identify '%s': %d, %s\n", DEVICE_FILE, errno, strerror(errno));
        return;
    }
    if (!S_ISCHR(st.st_mode)) {
        fprintf(stderr, "%s is not camera device ", DEVICE_FILE);
        return;
    }
    CameraFd = open(DEVICE_FILE, O_RDWR | O_NONBLOCK, 0);
    if (CameraFd == -1) {
        fprintf(stderr, "Cannot open '%s': %d, %s\\n", DEVICE_FILE, errno, strerror(errno));
        return;
    }


    struct v4l2_capability cap;
    struct v4l2_cropcap    cropcap;
    struct v4l2_crop       crop;
    struct v4l2_format     fmt;
    struct v4l2_streamparm stream_para;
    unsigned int min;



    memset(&stream_para, 0, sizeof(stream_para));
    stream_para.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (-1 == ioctl(CameraFd, VIDIOC_G_PARM, &stream_para)) {
        perror("ioctl VIDIOC_G_PARAM");
        close(CameraFd);
        return;
    }

    printf("numerator:%d\ndenominator:%d\n", stream_para.parm.capture.timeperframe.numerator, stream_para.parm.capture.timeperframe.denominator);
    CameraFps = stream_para.parm.capture.timeperframe.denominator;

    if (xioctl(CameraFd, VIDIOC_QUERYCAP, &cap) == -1)
    {
        fprintf(stderr, "get VIDIOC_QUERYCAP error: %d, %s\n", errno, strerror(errno));
        return;
    }

    if (!(cap.capabilities & V4L2_CAP_VIDEO_CAPTURE))
    {
        fprintf(stderr, "%s is not video capture device\n",DEVICE_FILE);
        return;
    }

    if (!(cap.capabilities & V4L2_CAP_STREAMING))
    {
        fprintf(stderr, "%s does not support streaming i/o\n", DEVICE_FILE);

        if (!(cap.capabilities & V4L2_CAP_READWRITE))
        {
            fprintf(stderr, "%s does not support read i/o\n", DEVICE_FILE);
            return;
        }
        io_method = 0;
    }
    else
    {
        io_method = 1;
    }


    /* Select video input, video standard and tune here. */


    memset(&cropcap, 0, sizeof(cropcap));
    cropcap.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (xioctl(CameraFd, VIDIOC_CROPCAP, &cropcap) == 0)
    {
        memset(&crop, 0, sizeof(crop));
        crop.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        crop.c = cropcap.defrect; /* reset to default */

        if (xioctl(CameraFd, VIDIOC_S_CROP, &crop) == -1)
        {
            fprintf(stderr, "set VIDIOC_S_CROP failed: %d, %s\n", errno, strerror(errno));
        }
    }
    else
    {
        fprintf(stderr, "get VIDIOC_CROPCAP failed: %d, %s\n", errno, strerror(errno));
    }

    /* Enum pixel format */
    for (int i = 0; i < 20; i++)
    {
        struct v4l2_fmtdesc fmtdesc;
        fmtdesc.index = i;
        fmtdesc.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        if (xioctl(CameraFd, VIDIOC_ENUM_FMT, &fmtdesc) == -1)
            break;

        printf("%d: %s\n", i, fmtdesc.description);
    }


    memset(&fmt, 0, sizeof(fmt));
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;


    if (ForceFormat)
    {
        fmt.fmt.pix.width = CamWidth;
        fmt.fmt.pix.height = CamHeight;
        fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_YUYV;
        fmt.fmt.pix.field = V4L2_FIELD_INTERLACED;

        if (xioctl(CameraFd, VIDIOC_S_FMT, &fmt) == -1)
        {
            fprintf(stderr, "get VIDIOC_S_FMT failed: %d, %s\n", errno, strerror(errno));
            return;
        }

        /* Note VIDIOC_S_FMT may change width and height. */
    }
    else
    {
        /* Preserve original settings as set by v4l2-ctl for example */
        if (xioctl(CameraFd, VIDIOC_G_FMT, &fmt) == -1)
        {
            fprintf(stderr, "get VIDIOC_G_FMT failed: %d, %s\n", errno, strerror(errno));
            return;
        }
    }

    /* Buggy driver paranoia. */
    min = fmt.fmt.pix.width * 2;
    if (fmt.fmt.pix.bytesperline < min)
        fmt.fmt.pix.bytesperline = min;
    min = fmt.fmt.pix.bytesperline * fmt.fmt.pix.height;
    if (fmt.fmt.pix.sizeimage < min)
        fmt.fmt.pix.sizeimage = min;

    if (io_method == 1) {
        //initMmap(CameraFd);
        struct v4l2_requestbuffers req;
        memset(&req, 0, sizeof(req));

        req.count = 4;
        req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        req.memory = V4L2_MEMORY_MMAP;

        if (xioctl(CameraFd, VIDIOC_REQBUFS, &req) == -1)
        {
            fprintf(stderr, "set VIDIOC_REQBUFS failed: %d, %s\n", errno, strerror(errno));
            return;
        }

        if (req.count < 2)
        {
            fprintf(stderr, "Insufficient buffer memory on %s\n",
                DEVICE_FILE);
            return;
        }

        buffers = (buffer*)calloc(req.count, sizeof(*buffers));

        if (!buffers)
        {
            fprintf(stderr, "Out of memory\n");
            return;
        }

        for (n_buffers = 0; n_buffers < req.count; ++n_buffers)
        {
            struct v4l2_buffer buf;
            memset(&buf, 0, sizeof(buf));

            buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
            buf.memory = V4L2_MEMORY_MMAP;
            buf.index = n_buffers;

            if (xioctl(CameraFd, VIDIOC_QUERYBUF, &buf) == -1)
            {
                fprintf(stderr, "set VIDIOC_QUERYBUF %u failed: %d, %s\n", n_buffers, errno, strerror(errno));
                return;
            }

            buffers[n_buffers].length = buf.length;
            buffers[n_buffers].start =
                mmap(NULL /* start anywhere */,
                    buf.length,
                    PROT_READ | PROT_WRITE /* required */,
                    MAP_SHARED /* recommended */,
                    CameraFd, buf.m.offset);
            //printf("aaaaaaaaaaaaaaaaaaaaaaaaaa");

            if (MAP_FAILED == buffers[n_buffers].start)
            {
                fprintf(stderr, "mmap %u failed: %d, %s\n", n_buffers, errno, strerror(errno));
                return;
            }
        }

    }
    unsigned int i;
    enum v4l2_buf_type type;
    for (i = 0; i < n_buffers; ++i)
    {
        struct v4l2_buffer buf;
        memset(&buf, 0, sizeof(buf));

        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = i;

        if (xioctl(CameraFd, VIDIOC_QBUF, &buf) == -1)
        {
            //printf("44444444");
            fprintf(stderr, "set VIDIOC_QBUF failed: %d, %s\n", errno, strerror(errno));
            return;
        }

    }
    type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (xioctl(CameraFd, VIDIOC_STREAMON, &type) == -1)
    {
        fprintf(stderr, "set VIDIOC_STREAMON failed: %d, %s\n", errno, strerror(errno));
        return;
    }
    //startCapturing(CameraFd);
    //main_loop(CameraFd);
    int r;
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(CameraFd, &fds);
    FILE* file = fopen("./outputoutput.h264", "wb+");
    //for (; i < 99; i++)
    for(;;)
    {
        struct timeval tv;
        tv.tv_sec = 2;
        tv.tv_usec = 0;

        fd_set rdset = fds;

        r = select(CameraFd + 1, &rdset, NULL, NULL, &tv);

        if (r > 0)
        {
            if (getCamFrame(CameraFd, file) == -2) {
                fprintf(stderr, "getCamFrame stderr \n");
                fflush(stderr);
                break;
            }
        }
        else if (r == 0)
        {
            fprintf(stderr, "select timeout\n");
            fflush(stderr);
        }
        else
        {
            if (EINTR == errno || EAGAIN == errno)
                continue;
            fprintf(stderr, "select failed: %d, %s\n", errno, strerror(errno));
            fflush(stderr);
            break;
        }
        /* EAGAIN - continue select loop. */
    }
    fclose(file);
    //stopCapturing(CameraFd);
    //enum v4l2_buf_type type;

    type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    xioctl(CameraFd, VIDIOC_STREAMOFF, &type);
    //destroyMpp();
    //freeBuf();
    //unsigned int i;

    for (i = 0; i < n_buffers; ++i)
        munmap(buffers[i].start, buffers[i].length);

    free(buffers);
    //closeCamFd(CameraFd);
    close(CameraFd);
}

int main(int argc, char* argv[])
{

    start();
    return 0;
}