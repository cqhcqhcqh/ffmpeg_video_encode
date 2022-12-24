#include "encodethread.h"
#include <QDebug>
#include <QFile>

#ifdef Q_OS_MAC
#define IN_FILE "/Users/chengqihan/Desktop/420p_out.yuv"
#define OUT_FILE "/Users/chengqihan/Desktop/out.h264"
#else
#define IN_FILE "C:\\Workspaces\\in.yuv"
#define OUT_FILE "C:\\Workspaces\\out.h264"
#endif

#define ERROR_BUF(res) \
    char errbuf[1024]; \
    av_strerror(res, errbuf, sizeof(errbuf)); \

EncodeThread::EncodeThread(QObject *parent) : QThread(parent) {
    connect(this, &QThread::finished, this, &QThread::deleteLater);
}

EncodeThread::~EncodeThread() {
    disconnect();
    requestInterruption();
    quit();
    wait();

    qDebug() << "EncodeThread::~EncodeThread()";
}

static int check_pix_fmt(const AVCodec *codec,
                         enum AVPixelFormat fmt)
{
    const enum AVPixelFormat *p = codec->pix_fmts;

    while (*p != AV_PIX_FMT_NONE) {
//        qDebug() << "fmt: " << av_get_sample_fmt_name(*p);
        if (*p == fmt)
            return 1;
        p++;
    }
    return 0;
}

int video_encode(AVCodecContext *ctx, AVFrame *frame, AVPacket *packet, QFile &out) {
    /// 这里只需要 send 一次（全量）
    int res = avcodec_send_frame(ctx, frame);
    if (res < 0) {
        ERROR_BUF(res);
        qDebug() << "avcodec_send_frame err:" << errbuf;
        return res;
    }
    qDebug() << "avcodec_send_frame success";

    /* read all the available output packets (in general there may be any
         * number of them */
    /// 这里需要批量的 receive？
    while (true) {
        res = avcodec_receive_packet(ctx, packet);
        // EAGAIN: output is not available in the current state - user must try to send input
        // AVERROR_EOF: the encoder has been fully flushed, and there will be no more output packets
        // 退出函数，重新走 send 流程
        if (res == AVERROR(EAGAIN) || res == AVERROR_EOF) {
           return 0;
        } else if (res < 0) {
           ERROR_BUF(res);
           qDebug() << "avcodec_receive_packet error" << errbuf;
           return res;
        }
        qDebug() << "avcodec_receive_packet success packet size: " << packet->size;
        out.write((char *) packet->data, packet->size);

        // 释放资源
        av_packet_unref(packet);
    }
}

/// PCM => AAC
void EncodeThread::run() {
    VideoEncodeSpec inSpec;
    inSpec.width = 640;
    inSpec.height = 480;
    inSpec.fmt = AV_PIX_FMT_YUV420P;
    inSpec.file = IN_FILE;
    inSpec.fps = 25;

    const AVCodec *codec = nullptr;
    AVCodecContext *ctx = nullptr;
    AVFrame *frame = nullptr;
    AVPacket *packet = nullptr;
    QFile yuv(inSpec.file);
    QFile h264(OUT_FILE);
    int res = 0;
    int frameBufferSize = av_image_get_buffer_size(inSpec.fmt,
                                                   inSpec.width,
                                                   inSpec.height,
                                                   1);;

    uint8_t *src = nullptr;

    codec = avcodec_find_encoder_by_name("libx264"); // AV_CODEC_ID_H264
    if (codec == nullptr) {
        qDebug() << "avcodec_find_encoder_by_name failure";
        return;
    }

    qDebug() << codec->name;

    if (!check_pix_fmt(codec, inSpec.fmt)) {
        //qDebug() << "check_pix_fmt not support pix fmt" << av_get_sample_fmt_name(inSpec.fmt);
        goto end;
    }

    ctx = avcodec_alloc_context3(codec);
    if (ctx == nullptr) {
        qDebug() << "avcodec_alloc_context3 failure";
        return;
    }

    /// 配置输入参数
    ctx->width = inSpec.width;
    ctx->height = inSpec.height;
    ctx->pix_fmt = inSpec.fmt;
    ctx->time_base = { 1, inSpec.fps };
    // 配置输出参数
    if (avcodec_open2(ctx, codec, nullptr) < 0) {
        ERROR_BUF(res);
        qDebug() << "avcodec_open2 error" << errbuf;
        goto end;
    }

    if (yuv.open(QFile::ReadOnly) == 0) {
        qDebug() << "pcm open failure file: " << inSpec.file;
        goto end;
    }

    if (h264.open(QFile::WriteOnly) == 0) {
        qDebug() << "aac open failure file: " << OUT_FILE;
        goto end;
    }

    /* frame containing input raw audio */
    frame = av_frame_alloc();
    if (frame == nullptr) {
        qDebug() << "av_frame_alloc failure";
        goto end;
    }

    frame->height = inSpec.width;
    frame->width = inSpec.width;
    frame->format = inSpec.fmt;
    frame->pts = 0;
    qDebug() << "frame_size" << ctx->frame_size;

//    res = av_image_alloc(frame->data,
//                         frame->linesize,
//                         inSpec.width,
//                         inSpec.height,
//                         inSpec.fmt, 1);
//    方法 2
    src = (uint8_t *)av_malloc(frameBufferSize);
    av_image_fill_arrays(frame->data,
                         frame->linesize,
                         src,
                         inSpec.fmt,
                         inSpec.width,
                         inSpec.height,
                         1);

//   方法 3：
//   用下面的这种方式来开辟初始化一段内存空间，来进行编码，编码出来的 h264 的结果播放出来的结果不对
//   res = av_frame_get_buffer(frame, 0);
    if (res < 0) {
        ERROR_BUF(res);
        qDebug() << "av_frame_get_buffer error" << errbuf;
        goto end;
    }

    packet = av_packet_alloc();
    if (packet == nullptr) {
        qDebug() << "av_packet_alloc failure";
        goto end;
    }

    while ((res = yuv.read((char *) frame->data[0],
                            frameBufferSize)) > 0) {
        qDebug() << "linesize: " << frame->linesize[0];
        int ret = video_encode(ctx, frame, packet, h264);
        if (ret < 0) {
            goto end;
        }

        // 使用 pts 来解决 `non-strictly-monotonic PTS` 控制台错误
        // 如果 pts 不自增的话，输出的 .h264 文件的大小和用 ffmpeg 命令行程序输出的文件结果也相差很大
        frame->pts++;
    }

    /*
     * It can be NULL, in which case it is considered a flush packet.
     * This signals the end of the stream. If the encoder
     * still has packets buffered, it will return them after this
     * call. Once flushing mode has been entered, additional flush
     * packets are ignored, and sending frames will return
     * AVERROR_EOF.
    */
    video_encode(ctx, nullptr, packet, h264);
end:
    yuv.close();
    h264.close();
    av_frame_free(&frame);
    av_packet_free(&packet);
    avcodec_free_context(&ctx);
}
