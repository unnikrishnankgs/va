#include "network.h"
#include "detection_layer.h"
#include "region_layer.h"
#include "cost_layer.h"
#include "utils.h"
#include "parser.h"
#include "box.h"
#include "image.h"
#include "demo.h"
#include <sys/time.h>
//#include <opencv2/opencv.hpp>

#define DEMO 1

/** { ISVA */
//#define ENABLE_VIDEO_FILE_READ_AT_TAR_FPS
//#define DISPLAY_RESULS

#define DEBUG
#define VERBOSE
#include "debug.h"

typedef struct
{
     int x;
     int y;
     int w;
     int h;
     char* pcClassName;
     double fCurrentFrameTimeStamp;
     int nVideoId;
     double prob;
}tAnnInfo;

typedef int (*tfnRaiseAnnCb)(tAnnInfo apAnnInfo);
typedef struct
{
    char* pcCfg; /**< yolo.cfg */
    char* pcWeights; /**< yolo.weights */
    char* pcFileName; /**< .mp4 file */
    char* pcDataCfg; /**< say, coco.data */
    double fTargetFps; /**< 1 fps */
    double fThresh; /**< .24 */
    tfnRaiseAnnCb pfnRaiseAnnCb;
    int nVideoId;
    int isVideo;
}tDetectorModel;
/** } ISVA */

static tDetectorModel* pDetectorModel;

#ifdef OPENCV

static char **demo_names;
static image **demo_alphabet;
static int demo_classes;

static float **probs;
static box *boxes;
static network net;
static image buff [3];
static double buff_ts [3];
static image buff_letter[3];
static int buff_index = 0;
static CvCapture * cap;
static IplImage  * ipl;
static float fps = 0;
static float demo_thresh = 0;
static float demo_hier = .5;
static int running = 0;

static int demo_delay = 0;
static int demo_frame = 3;
static int demo_detections = 0;
static float **predictions;
static int demo_index = 0;
static int demo_done = 0;
static float *last_avg2;
static float *last_avg;
static float *avg;
double demo_time;


static double nTargetFps = 1;
static int nCurFrameCount = 0;
static double nFps = 0;
static int nSkipFramesCnt = 0;
static int bProcessThisFrame = 0;

static void test_detector_on_img(char *datacfg, char *cfgfile, char *weightfile, char *filename, float thresh, float hier_thresh, char *outfile, int fullscreen);

void init_globals()
{
    demo_names = NULL;
    demo_alphabet = NULL;
    demo_classes =  0;
    
    probs = NULL;
    boxes = NULL;
    buff_ts[0] = 0;
    buff_ts[1] = 0;
    buff_ts[3] = 0;
    buff_index = 0;
    cap = 0;
    ipl = NULL;
    fps = 0;
    demo_thresh = 0;
    demo_hier = .5;
    running = 0;
    
    demo_delay = 0;
    demo_frame = 3;
    demo_detections = 0;
    predictions = NULL;
    demo_index = 0;
    demo_done = 0;
    last_avg2 = NULL;
    last_avg = NULL;
    avg = NULL;
    demo_time = 0;
    
    
    nTargetFps = 1;
    nCurFrameCount = 0;
    nFps = 0;
    nSkipFramesCnt = 0;
    bProcessThisFrame = 0;
}

double get_wall_time()
{
    struct timeval time;
    if (gettimeofday(&time,NULL)){
        return 0;
    }
    return (double)time.tv_sec + (double)time.tv_usec * .000001;
}

void evaluate_detections(image im, int num, float thresh, box *boxes, float **probs, char **names, image **alphabet, int classes)
{
    int i;
    tAnnInfo annInfo;

    for(i = 0; i < num; ++i){
        int class_ = max_index(probs[i], classes);
        float prob = probs[i][class_];
        LOGD("%s: %.0f%%\n", names[class_], prob*100);
        if(prob > thresh){

            int width = im.h * .006;

            if(0){
                width = pow(prob, 1./2.)*10+1;
                alphabet = 0;
            }

            //LOGD("%d %s: %.0f%%\n", i, names[class_], prob*100);
            LOGD("%s: %.0f%%\n", names[class_], prob*100);
            int offset = class_*123457 % classes;
            float red = get_color(2,offset,classes);
            float green = get_color(1,offset,classes);
            float blue = get_color(0,offset,classes);
            float rgb[3];

            //width = prob*20+2;

            rgb[0] = red;
            rgb[1] = green;
            rgb[2] = blue;
            box b = boxes[i];

            int w, h;
            w = cvGetCaptureProperty(cap, CV_CAP_PROP_FRAME_WIDTH);
            h = cvGetCaptureProperty(cap, CV_CAP_PROP_FRAME_HEIGHT);
            int left  = (b.x-b.w/2.)*w;
            int right = (b.x+b.w/2.)*w;
            int top   = (b.y-b.h/2.)*h;
            int bot   = (b.y+b.h/2.)*h;

            if(left < 0) left = 0;
            if(right > im.w-1) right = im.w-1;
            if(top < 0) top = 0;
            if(bot > im.h-1) bot = im.h-1;

#ifdef DISPLAY_RESULS
            draw_box_width(im, left, top, right, bot, width, red, green, blue);
            if (alphabet) {
                image label = get_label(alphabet, names[class_], (im.h*.03)/10);
                draw_label(im, top + width, left, label, rgb);
                free_image(label);
            }
#endif
            LOGV("box x:%f y:%f w:%f h:%f; l:%d r:%d t:%d b:%d\n", b.x, b.y, b.w, b.h, left, right, top, bot);
            annInfo.x = (int)(left);
            LOGV("box x:%f\n", annInfo.x);
            annInfo.y = (int)(top);
            annInfo.w = (int)(right - left);
            annInfo.h = (int)(bot - top);
            annInfo.pcClassName = (char*)malloc(strlen(names[class_]) + 1);
            strcpy(annInfo.pcClassName, names[class_]);
            annInfo.fCurrentFrameTimeStamp = buff_ts[(buff_index+2)%3];
            annInfo.nVideoId = pDetectorModel->nVideoId;
            annInfo.prob = prob;
            LOGD("hello..");
            LOGV("annInfo x=%d y=%d w=%d h=%d pcClassName=%s\n",
                annInfo.x, annInfo.y, annInfo.w, annInfo.h, annInfo.pcClassName);
            pDetectorModel->pfnRaiseAnnCb(annInfo);
        }
    }
}

void *detect_in_thread(void *ptr)
{
    LOGD("DEBUGME\n");
    running = 1;
    float nms = .4;

    layer l = net.layers[net.n-1];
    float *X = buff_letter[(buff_index+2)%3].data;
    float *prediction = network_predict(net, X);

    memcpy(predictions[demo_index], prediction, l.outputs*sizeof(float));
    mean_arrays(predictions, demo_frame, l.outputs, avg);
    l.output = last_avg2;
    if(demo_delay == 0) l.output = avg;
    if(l.type == DETECTION){
        LOGD("DETECTION!\n\n\n\n");
        get_detection_boxes(l, 1, 1, demo_thresh, probs, boxes, 0);
    } else if (l.type == REGION){
        LOGD("REGION! buf[0].w=%d h=%d net.w=%d h=%d\n\n\n\n",
            buff[0].w,
            buff[0].h,
            net.w,
            net.h
            );
        get_region_boxes(l, buff[0].w, buff[0].h, net.w, net.h, demo_thresh, probs, boxes, 0, 0, demo_hier, 1);
    } else {
        error("Last layer must produce detections\n");
    }
    if (nms > 0) do_nms_obj(boxes, probs, l.w*l.h*l.n, l.classes, nms);

    //LOGD("\033[2J");
    //LOGD("\033[1;1H");
    //LOGD("\nFPS:%.1f\n",fps);
    LOGD("Objects:\n\n");
    image display = buff[(buff_index+2) % 3];
    LOGD("Draw detections demo_detections=%d demo_classes=%d demo_thresh=%f\n", demo_detections, demo_classes, demo_thresh);
    #ifdef DISPLAY_RESULS
    draw_detections(display, demo_detections, demo_thresh, boxes, probs, demo_names, demo_alphabet, demo_classes);
    #endif
    evaluate_detections(display, demo_detections, demo_thresh, boxes, probs, demo_names, demo_alphabet, demo_classes);
    
    demo_index = (demo_index + 1)%demo_frame;
    LOGD("demo_index=%d; demo_frame=%d\n", demo_index, demo_frame);
    running = 0;
    return 0;
}

void *fetch_in_thread(void *ptr)
{
    int status;

    buff_ts[buff_index] = cvGetCaptureProperty(cap, CV_CAP_PROP_POS_MSEC);
    status = fill_image_from_stream(cap, buff[buff_index]);
    letterbox_image_into(buff[buff_index], net.w, net.h, buff_letter[buff_index]);
    if(status == 0) demo_done = 1;
    return 0;
}

void *display_in_thread(void *ptr)
{
    show_image_cv(buff[(buff_index + 1)%3], "Demo", ipl);
    int c = cvWaitKey(1);
    if (c != -1) c = c%256;
    if (c == 10){
        if(demo_delay == 0) demo_delay = 60;
        else if(demo_delay == 5) demo_delay = 0;
        else if(demo_delay == 60) demo_delay = 5;
        else demo_delay = 0;
    } else if (c == 27) {
        demo_done = 1;
        return 0;
    } else if (c == 82) {
        demo_thresh += .02;
    } else if (c == 84) {
        demo_thresh -= .02;
        if(demo_thresh <= .02) demo_thresh = .02;
    } else if (c == 83) {
        demo_hier += .02;
    } else if (c == 81) {
        demo_hier -= .02;
        if(demo_hier <= .0) demo_hier = .0;
    }
    return 0;
}

void *display_loop(void *ptr)
{
    while(1){
        display_in_thread(0);
    }
}

void *detect_loop(void *ptr)
{
    while(1){
        detect_in_thread(0);
    }
}

void demo(char *cfgfile, char *weightfile, float thresh, int cam_index, const char *filename, char **names, int classes, int delay, char *prefix, int avg_frames, float hier, int w, int h, int frames, int fullscreen)
{
    int res = 0;

    demo_delay = delay;
    demo_frame = avg_frames;
    predictions = (float**)calloc(demo_frame, sizeof(float*));
    image **alphabet = NULL;
#ifdef DISPLAY_RESULS
    alphabet = load_alphabet();
#endif
    demo_names = names;
    demo_alphabet = alphabet;
    demo_classes = classes;
    demo_thresh = thresh;
    demo_hier = hier;
    LOGD("Demo\n");
    LOGD("classes=%d delay=%d avg_frames=%d hier=%f w=%d h=%d frames=%d fullscreen=%d\n", classes, delay, avg_frames, hier, w, h, frames, fullscreen);
    net = parse_network_cfg(cfgfile);
    if(weightfile){
        load_weights(&net, weightfile);
    }
    set_batch_network(&net, 1);
    pthread_t detect_thread;
    pthread_t fetch_thread;

    srand(2222222);


    if(filename){
        LOGD("video file: %s\n", filename);
        cap = cvCaptureFromFile(filename);
        LOGD("DEBUGME %p\n", cap);
    }else{
        cap = cvCaptureFromCAM(cam_index);

        if(w){
            cvSetCaptureProperty(cap, CV_CAP_PROP_FRAME_WIDTH, w);
        }
        if(h){
            cvSetCaptureProperty(cap, CV_CAP_PROP_FRAME_HEIGHT, h);
        }
        if(frames){
            cvSetCaptureProperty(cap, CV_CAP_PROP_FPS, frames);
        }
    }

    if(!cap)
    {
        //error("Couldn't connect to webcam.\n");
        LOGE("ERROR; file could not be read / dev could not be opened\n");
        return;
    }

    layer l = net.layers[net.n-1];
    demo_detections = l.n*l.w*l.h;
    int j;

    LOGD("DEBUGME\n");
    avg = (float *) calloc(l.outputs, sizeof(float));
    last_avg  = (float *) calloc(l.outputs, sizeof(float));
    last_avg2 = (float *) calloc(l.outputs, sizeof(float));
    for(j = 0; j < demo_frame; ++j) predictions[j] = (float *) calloc(l.outputs, sizeof(float));

    boxes = (box *)calloc(l.w*l.h*l.n, sizeof(box));
    probs = (float **)calloc(l.w*l.h*l.n, sizeof(float *));
    for(j = 0; j < l.w*l.h*l.n; ++j) probs[j] = (float *)calloc(l.classes+1, sizeof(float));

    buff_ts[0] = cvGetCaptureProperty(cap, CV_CAP_PROP_POS_MSEC);
    buff[0] = get_image_from_stream(cap);
    buff[1] = copy_image(buff[0]);
    buff[2] = copy_image(buff[0]);
    buff_letter[0] = letterbox_image(buff[0], net.w, net.h);
    buff_letter[1] = letterbox_image(buff[0], net.w, net.h);
    buff_letter[2] = letterbox_image(buff[0], net.w, net.h);
    ipl = cvCreateImage(cvSize(buff[0].w,buff[0].h), IPL_DEPTH_8U, buff[0].c);

    LOGD("DEBUGME\n");
    int count = 0;
    #ifdef DISPLAY_RESULS
    if(!prefix){
        cvNamedWindow("Demo", CV_WINDOW_NORMAL); 
        if(fullscreen){
            cvSetWindowProperty("Demo", CV_WND_PROP_FULLSCREEN, CV_WINDOW_FULLSCREEN);
        } else {
            cvMoveWindow("Demo", 0, 0);
            cvResizeWindow("Demo", 1352, 1013);
        }
    }
    #endif

    demo_time = get_wall_time();

    nFps = (int)cvGetCaptureProperty(cap, CV_CAP_PROP_FPS);
    
    nSkipFramesCnt = (int)(nFps / nTargetFps);

    //cvSetCaptureProperty(cap, CV_CAP_PROP_FPS, (double)nTargetFps);

    LOGD("DEBUGME %d\n", demo_done);
    while(!demo_done){
        LOGD("demo_done=%d count=%d prefix=%s nSkipFramesCnt=%d\n", demo_done, count, prefix, nSkipFramesCnt);
        LOGD("cap prop; w=%f h=%f frame_count=%f FPS=%f POS_MS=%f pos_count=%f\n", 
                cvGetCaptureProperty(cap, CV_CAP_PROP_FRAME_WIDTH),
                cvGetCaptureProperty(cap, CV_CAP_PROP_FRAME_HEIGHT),
                cvGetCaptureProperty(cap, CV_CAP_PROP_FRAME_COUNT),
                cvGetCaptureProperty(cap, CV_CAP_PROP_FPS),
                cvGetCaptureProperty(cap, CV_CAP_PROP_POS_MSEC),
                cvGetCaptureProperty(cap, CV_CAP_PROP_POS_FRAMES));
#ifdef ENABLE_VIDEO_FILE_READ_AT_TAR_FPS
        bProcessThisFrame = (nCurFrameCount && !(nCurFrameCount % nSkipFramesCnt));
        if(bProcessThisFrame)
#else
        if(1)
#endif /**< ENABLE_VIDEO_FILE_READ_AT_TAR_FPS */
        {
            buff_index = (buff_index + 1) %3;
            if(pthread_create(&fetch_thread, 0, fetch_in_thread, 0)) error("Thread creation failed");
            if(pthread_create(&detect_thread, 0, detect_in_thread, 0)) error("Thread creation failed");
            if(!prefix){
                if(count % (demo_delay+1) == 0){
                    fps = 1./(get_wall_time() - demo_time);
                    demo_time = get_wall_time();
                    float *swap = last_avg;
                    last_avg  = last_avg2;
                    last_avg2 = swap;
                    memcpy(last_avg, avg, l.outputs*sizeof(float));
                }
                #ifdef DISPLAY_RESULS
                display_in_thread(0);
                #endif /**< DISPLAY_RESULS*/
            }else{
                char name[256];
                //LOGD(name, "%s_%08d", prefix, count);
                save_image(buff[(buff_index + 1)%3], name);
            }
            pthread_join(fetch_thread, 0);
            pthread_join(detect_thread, 0);
            ++count;
        }
        else
        {
            cvGrabFrame(cap);
        }
        LOGD("DEBUGME\n");
        nCurFrameCount++;
    }
    LOGD("DEBUGME\n");
    //cvReleaseCapture(cap);
    LOGD("DEBUGME\n");
    cap = NULL;
}
#else
void demo(char *cfgfile, char *weightfile, float thresh, int cam_index, const char *filename, char **names, int classes, int delay, char *prefix, int avg, float hier, int w, int h, int frames, int fullscreen)
{
    fLOGD(stderr, "Demo needs OpenCV for webcam images.\n");
}
#endif

int run_detector_model(tDetectorModel* apDetectorModel)
{
    init_globals();
#if 0
    char *prefix = find_char_arg(argc, argv, "-prefix", 0);
    float thresh = find_float_arg(argc, argv, "-thresh", .24);
    float hier_thresh = find_float_arg(argc, argv, "-hier", .5);
    int cam_index = find_int_arg(argc, argv, "-c", 0);
    int frame_skip = find_int_arg(argc, argv, "-s", 0);
    int avg = find_int_arg(argc, argv, "-avg", 3);
    if(argc < 4){
        fLOGD(stderr, "usage: %s %s [train/test/valid] [cfg] [weights (optional)]\n", argv[0], argv[1]);
        return;
    }
    char *gpu_list = find_char_arg(argc, argv, "-gpus", 0);
    char *outfile = find_char_arg(argc, argv, "-out", 0);
    int *gpus = 0;
    int gpu = 0;
    int ngpus = 0;
    if(gpu_list){
        LOGD("%s\n", gpu_list);
        int len = strlen(gpu_list);
        ngpus = 1;
        int i;
        for(i = 0; i < len; ++i){
            if (gpu_list[i] == ',') ++ngpus;
        }
        gpus = calloc(ngpus, sizeof(int));
        for(i = 0; i < ngpus; ++i){
            gpus[i] = atoi(gpu_list);
            gpu_list = strchr(gpu_list, ',')+1;
        }
    } else {
        gpu = gpu_index;
        gpus = &gpu;
        ngpus = 1;
    }

    int clear = find_arg(argc, argv, "-clear");
    int fullscreen = find_arg(argc, argv, "-fullscreen");
    int width = find_int_arg(argc, argv, "-w", 0);
    int height = find_int_arg(argc, argv, "-h", 0);
    int fps = find_int_arg(argc, argv, "-fps", 0);

    char *datacfg = argv[3];
    char *cfg = argv[4];
    char *weights = (argc > 5) ? argv[5] : 0;
    char *filename = (argc > 6) ? argv[6]: 0;
    if(0==strcmp(argv[2], "test")) test_detector(datacfg, cfg, weights, filename, thresh, hier_thresh, outfile, fullscreen);
    else if(0==strcmp(argv[2], "train")) train_detector(datacfg, cfg, weights, gpus, ngpus, clear);
    else if(0==strcmp(argv[2], "valid")) validate_detector(datacfg, cfg, weights, outfile);
    else if(0==strcmp(argv[2], "valid2")) validate_detector_flip(datacfg, cfg, weights, outfile);
    else if(0==strcmp(argv[2], "recall")) validate_detector_recall(cfg, weights);
    else if(0==strcmp(argv[2], "demo")) {
        list *options = read_data_cfg(datacfg);
        int classes = option_find_int(options, "classes", 20);
        char *name_list = option_find_str(options, "names", "data/names.list");
        char **names = get_labels(name_list);
        demo(cfg, weights, thresh, cam_index, filename, names, classes, frame_skip, prefix, avg, hier_thresh, width, height, fps, fullscreen);
    }
#endif
    {
        pDetectorModel = apDetectorModel;
        LOGD("isVideo=%d\n", apDetectorModel->isVideo);
        if(apDetectorModel->isVideo)
        {
            LOGD("in %p\n", apDetectorModel);
            LOGD("demo start %s\n", apDetectorModel->pcDataCfg);
            list *options = read_data_cfg(apDetectorModel->pcDataCfg);
            LOGD("h1\n");
            char *name_list = option_find_str(options, "names", "data/names.list");
            LOGD("name_list=%s\n", name_list);
            char **names = get_labels(name_list);
            LOGD("h1\n");
            int classes = option_find_int(options, "classes", 20);
            LOGD("h1\n");
            LOGD("Detector Model cb is %p\n", pDetectorModel->pfnRaiseAnnCb);
            demo(apDetectorModel->pcCfg, apDetectorModel->pcWeights, 0.24/**< apDetectorModel->fThresh */, 
                0/**< cam_index */, apDetectorModel->pcFileName, names, classes, 0 /**< frame_skip */, 
                NULL, 3, demo_hier, 0 /**< w */, 0 /**< h */, 0 /**< 0 */, 0 /**< fullscreen */
                );
        }
        else
        {
            test_detector_on_img(apDetectorModel->pcDataCfg, apDetectorModel->pcCfg, apDetectorModel->pcWeights, apDetectorModel->pcFileName, apDetectorModel->fThresh, demo_hier, NULL, 0);
        }

       
    }

    return 0;
}

static void test_detector_on_img(char *datacfg, char *cfgfile, char *weightfile, char *filename, float thresh, float hier_thresh, char *outfile, int fullscreen)
{
    LOGD("DEBUGME\n");
    list *options = read_data_cfg(datacfg);
    LOGD("DEBUGME\n");
    char *name_list = option_find_str(options, "names", "data/names.list");
    LOGD("DEBUGME\n");
    char **names = get_labels(name_list);
    LOGD("DEBUGME\n");

    image **alphabet = NULL;
#ifdef DISPLAY_RESULS
    alphabet = load_alphabet();
#endif
    LOGD("DEBUGME\n");
    network net = parse_network_cfg(cfgfile);
    LOGD("DEBUGME\n");
    if(weightfile){
        load_weights(&net, weightfile);
    }
    set_batch_network(&net, 1);
    srand(2222222);
    clock_t time;
    char buff[2560];
    char *input = buff;
    int j;
    float nms=.4;
    LOGD("DEBUGME\n");
    while(1){
    LOGD("DEBUGME [%s]\n", filename);
        if(filename){
            strncpy(input, filename, 2560);
            LOGD("input [%s]\n", input);
        } else {
            printf("Enter Image Path: ");
            fflush(stdout);
            input = fgets(input, 256, stdin);
            if(!input) return;
            strtok(input, "\n");
        }
        LOGD("DEBUGME\n");
        image im = load_image_color(input,0,0);
        LOGD("DEBUGME im.data=%p\n", im.data);
        image sized = letterbox_image(im, net.w, net.h);
        LOGD("DEBUGME\n");
        //image sized = resize_image(im, net.w, net.h);
        //image sized2 = resize_max(im, net.w);
        //image sized = crop_image(sized2, -((net.w - sized2.w)/2), -((net.h - sized2.h)/2), net.w, net.h);
        //resize_network(&net, sized.w, sized.h);
        layer l = net.layers[net.n-1];

        box *boxes = (box*)calloc(l.w*l.h*l.n, sizeof(box));
        float **probs = (float**)calloc(l.w*l.h*l.n, sizeof(float *));
        for(j = 0; j < l.w*l.h*l.n; ++j) probs[j] = (float*)calloc(l.classes + 1, sizeof(float *));

        float *X = sized.data;
        time=clock();
        network_predict(net, X);
        printf("%s: Predicted in %f seconds.\n", input, sec(clock()-time));
        get_region_boxes(l, im.w, im.h, net.w, net.h, thresh, probs, boxes, 0, 0, hier_thresh, 1);
        if (nms) do_nms_obj(boxes, probs, l.w*l.h*l.n, l.classes, nms);
        //else if (nms) do_nms_sort(boxes, probs, l.w*l.h*l.n, l.classes, nms);
        evaluate_detections(im, l.w*l.h*l.n, thresh, boxes, probs, names, alphabet, l.classes);
#ifdef DISPLAY_RESULS
        draw_detections(im, l.w*l.h*l.n, thresh, boxes, probs, names, alphabet, l.classes);
        if(outfile){
            save_image(im, outfile);
        }
        else{
            save_image(im, "predictions");
#ifdef OPENCV
            cvNamedWindow("predictions", CV_WINDOW_NORMAL); 
            if(fullscreen){
                cvSetWindowProperty("predictions", CV_WND_PROP_FULLSCREEN, CV_WINDOW_FULLSCREEN);
            }
            show_image(im, "predictions");
            cvWaitKey(0);
            cvDestroyAllWindows();
#endif
        }
#endif

        free_image(im);
        free_image(sized);
        free(boxes);
        free_ptrs((void **)probs, l.w*l.h*l.n);
        if (filename) break;
    }
}

