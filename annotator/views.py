from django.shortcuts import render, redirect
from django.conf import settings
from django.http import HttpResponse, Http404, HttpResponseBadRequest, HttpResponseForbidden
from django.views.generic import View
from django.views.decorators.clickjacking import xframe_options_exempt
from django.contrib.admin.views.decorators import staff_member_required
from django.core.exceptions import ObjectDoesNotExist
from mturk.queries import get_active_video_turk_task
from .models import *
from mturk.models import Task, FullVideoTask, SingleFrameTask
from .services import *
from datetime import datetime, timezone

import os
import json
import urllib.request
import urllib.parse
import markdown
import sys
import mturk.utils
import sqlite3
from mturk.queries import get_active_video_turk_task
from .models import *
from .services import *

import logging
import ast

from django.contrib import auth
#from django.contrib.auth import models.User
from django.contrib.auth.models import User
from django.db.models import Max

#For darknet:
from ctypes import *

logger = logging.getLogger()


def home(request):
    need_annotating = Video.objects.filter(id__gt=0, verified=False)
    return render(request, 'video_list.html', context={
        'videos': need_annotating,
        'thumbnail': True,
        'test': settings.AWS_ID,
        'title': 'Videos'
    })

def verify_list(request):
    need_verification = Video.objects.filter(id__gt=0, verified=False).exclude(annotation='')[:250]
    return render(request, 'video_list.html', context={
        'videos': need_verification,
        'title': 'Videos to Verify'
    })

def verified_list(request):
    verified = Video.objects.filter(id__gt=0, verified=True).exclude(annotation='')[:100]
    return render(request, 'video_list.html', context={
        'videos': verified,
        'title': 'Verified Videos'
    })

def ready_to_pay(request):
    #tasks = FullVideoTask.objects.filter(paid = False, video__verified = True).exclude(hit_id = '')
    tasks = FullVideoTask.objects.all()#filter(paid = False, video__verified = True).exclude(hit_id = '')
    print("there are {} tasks".format(len(tasks)))
    return render(request, 'turk_ready_to_pay.html', context={
        'tasks': tasks,
    })

def next_unannotated(request, video_id):
    id = Video.objects.filter(id__gt=video_id, annotation='')[0].id
    return redirect('video', id)

# status of Not Published, Published, Awaiting Approval, Verified
# this is a bit convoluted as there's status stored on
# video (approved) as well as FullVideoTask (closed, paid, etc.)
def get_mturk_status(video, full_video_task):
    if video.verified:
        return "Verified"
    if full_video_task == None:
        if video.rejected == True:
            return "Rejected"
        elif video.annotation == '':
            return "Not Published"
        else:
            return "Awaiting Approval"
    if full_video_task.worker_id == '':
        return "Published"
    if full_video_task.worker_id != '':
        return "Awaiting Approval"

@xframe_options_exempt
def video(request, video_id):
    try:
        video = Video.objects.get(id=video_id)
        labels = Label.objects.all()
    except Video.DoesNotExist:
        raise Http404('No video with id "{}". Possible fixes: \n1) Download an up to date DB, see README. \n2) Add this video to the DB via /admin'.format(video_id))

    mturk_data = mturk.utils.authenticate_hit(request)
    if 'error' in mturk_data:
        return HttpResponseForbidden(mturk_data['error'])
    if not (mturk_data['authenticated'] or request.user.is_authenticated()):
        return redirect('/login/?next=' + request.path)

    try:
        cuser = User.objects.get(username=request.user);
        print("user is ", cuser.id);
    except User.DoesNotExist:
        raise Http404('No user with name "{}"'.format(request.user))

    start_time = float(request.GET['s']) if 's' in request.GET else None
    end_time = float(request.GET['e']) if 'e' in request.GET else None

    turk_task = get_active_video_turk_task(video.id)

    if turk_task != None:
        if turk_task.metrics != '':
            metricsDictr = ast.literal_eval(turk_task.metrics)
        else:
            metricsDictr = {}

        # Data for Javascript
        full_video_task_data = {
            'id': turk_task.id,
            'storedMetrics': metricsDictr,
            'bonus': float(turk_task.bonus),
            'bonusMessage': turk_task.message,
            'rejectionMessage': settings.MTURK_REJECTION_MESSAGE,
            'emailSubject': settings.MTURK_EMAIL_SUBJECT,
            'emailMessage': settings.MTURK_EMAIL_MESSAGE,
            'isComplete': turk_task.worker_id != ''
        }

        # Data for python templating
        if turk_task.last_email_sent_date != None:
            mturk_data['last_email_sent_date'] = turk_task.last_email_sent_date.strftime("%Y-%m-%d %H:%M")
    else:
        full_video_task_data = None

    mturk_data['status'] = get_mturk_status(video, turk_task)
    mturk_data['has_current_full_video_task'] = full_video_task_data != None

    video_data = json.dumps({
        'id': video.id,
        'location': video.url,
        'path': video.host,
        'is_image_sequence': True if video.image_list else False,
        'annotated': video.annotation != '',
        'verified': video.verified,
        'rejected': video.rejected,
        'start_time': start_time,
        'end_time' : end_time,
        'turk_task' : full_video_task_data
    })

    label_data = []
    for l in labels:
        label_data.append({'name': l.name, 'color': l.color})

    help_content = ''
    if settings.HELP_URL and settings.HELP_USE_MARKDOWN:
        help_content = urllib.request.urlopen(settings.HELP_URL).read().decode('utf-8')
        help_content = markdown.markdown(help_content)

    response = render(request, 'video.html', context={
        'label_data': label_data,
        'video_data': video_data,
        'image_list': json.loads(video.image_list) if video.image_list else 0,
        #'image_list_path': urllib.parse.quote(video.host),
        'image_list_path': video.host,
        'help_url': settings.HELP_URL,
        'help_embed': settings.HELP_EMBED,
        'mturk_data': mturk_data,
        'iframe_mode': mturk_data['authenticated'],
        'survey': False,
        'help_content': help_content,
        'current_user' : cuser.id
    })
    if not mturk_data['authenticated']:
        response['X-Frame-Options'] = 'SAMEORIGIN'
    return response


class AnnotationView(View):

    def get(self, request, video_id):
        video = Video.objects.get(id=video_id)
        print("annotation is", video.annotation);
        return HttpResponse(video.annotation, content_type='application/json')

    def post(self, request, video_id):
        data = json.loads(request.body.decode('utf-8'))

        video = Video.objects.get(id=video_id)
        print("request user is ", request.user);
        try:
            cuser = User.objects.get(username=request.user);
            print("user is here ", cuser.id);
        except User.DoesNotExist:
            raise Http404('No user with name "{}"'.format(request.user))

        print("annotation is ", json.dumps(data['annotation']));
        """
        #Note: the design is such that data['annotation'] will be annotations from user-id == cuser.id
        data_annotation_final = list(data['annotation']); #make a copy of data['annotation']

        # we need to understand who drive this data
        # delete all annotation from this user in video.annotation
        # and take in the new annotation dataset on his behalf
        print(video.annotation);
        if video.annotation:
            annotation_py = list(json.loads(video.annotation));
            annotation_py_final = list(json.loads(video.annotation));
            for annotation_itemm in annotation_py:
                print("annotation_itemm is ", annotation_itemm, "user id is ", cuser.id);
                if 'user_info' in annotation_itemm and annotation_itemm['user_info'] and int(annotation_itemm['user_info']['user_id']) == int(cuser.id):
                    print("same user id", cuser.id);
                    annotation_py_final.remove(annotation_itemm);
            print("annotation_py now ", annotation_py_final);
            if data_annotation_final:
                annotation_py_final = annotation_py_final + data_annotation_final; #data['annotation'] will have only annotations from current user now
        elif data_annotation_final:
            annotation_py_final = data_annotation_final;
        """
        annotation_py_final = data['annotation']
        if annotation_py_final:
            video.annotation = json.dumps(annotation_py_final)
            video.save()

        max_user_id = User.objects.all().aggregate(Max('id'))
        print("max_user_id is ", max_user_id['id__max'])

        #------ DataSet dump code starts here
        conn=sqlite3.connect('db.sqlite3')
        conn.row_factory=sqlite3.Row
        c=conn.cursor()
        #TODO add userinfo in file_name feature after grouping feature addition
        #command = 'SELECT annotation FROM annotator_video ' + 'where id=' + i; 
        c.execute('SELECT annotation FROM annotator_video')
        res=c.fetchall()
        rows=[dict(re) for re in res]
        base = "{'annotation': ''}";
        for i in range(0, rows.__len__()):
            r = str(rows[i]['annotation']);
            #print("lengths", r.__len__(), base.__len__());
            if r.__len__() > base.__len__():
                print("r is ", r);
                if 'id__max' in max_user_id and max_user_id['id__max']:
                    for j in range(1, max_user_id['id__max'] + 1):
                        file_name = "dataset/" + "video_" + str(i) + "_" + str(j) + ".json";
                        print(file_name);
                        current_user_annotation = getUserAnnotation(j, r)
                        if current_user_annotation:
                            print("current_user_annotation=", current_user_annotation);
                            f = open(file_name, 'w');
                            f.write(current_user_annotation);
                            f.close();
        #rows_json=json.dumps(rows)
        #rj=json.loads(rows_json)
        #print("all annotations:{", rj, "}")
        #------- ends here

        hit_id = data.get('hitId', None)
        if hit_id != None:
            if not Task.valid_hit_id(hit_id):
                return HttpResponseForbidden('Not authenticated')
            else:
                try:
                    worker_id = data.get('workerId', '')
                    assignment_id = data.get('assignmentId', '')
                    task = Task.get_by_hit_id(hit_id)
                    task.complete(worker_id, assignment_id, data['metrics'])
                except ObjectDoesNotExist:
                    if not settings.DEBUG:
                        raise
        return HttpResponse('success')


class ReceiveCommand(View):

    def post(self, request, video_id):
        data = json.loads(request.body.decode('utf-8'))

        try:
            vid_id = int(video_id)
            command_type = data['type']

            if 'bonus' in data:
                bonus = data['bonus']
            message = data['message']
            reopen = data['reopen']
            delete_boxes = data['deleteBoxes']
            block_worker = data['blockWorker']
            updated_annotations = json.dumps(data['updatedAnnotations'])

            if command_type == "accept":
                accept_video(request, vid_id, bonus, message, reopen, delete_boxes, block_worker, updated_annotations)
            elif command_type == "reject":
                reject_video(request, vid_id, message, reopen, delete_boxes, block_worker, updated_annotations)
            elif command_type == "email":
                email_worker(request, vid_id, data['subject'], message)

            return HttpResponse(status=200)
        except Exception as e:
            logger.exception(e)
            response = HttpResponse(status=500)
            response['error-message'] = str(e)
            return response
from django.utils.encoding import smart_str

def download_annotation(request, video_id):
    import mimetypes
    import os.path
    mimetypes.init()
    print("request for download", request.user);
    try:
        cuser = User.objects.get(username=request.user);
        print("user is here ", cuser.id);
    except User.DoesNotExist:
        raise Http404('No user with name "{}"'.format(request.user))

    try:
        file_path = settings.BASE_DIR + '/' + 'dataset/video_' + video_id + '_' + str(cuser.id) + '.json';
 
        print("file_path:", file_path)
        filesock = open(file_path, 'r')
        file_name = os.path.basename(file_path)
        file_size = os.path.getsize(file_path)
        mime_type_guess = mimetypes.guess_type(file_name)
        print("mime", mime_type_guess, "file_size", file_size, "file_name", file_name);
        print(mime_type_guess)
        content = filesock.read()
        #print("content is ", content)
        if mime_type_guess is not None:
            response = HttpResponse(content, content_type=mime_type_guess[0])
        #response = HttpResponse(filesock, content_type='application/force-download')
        response['Content-Disposition'] = 'attachment; filename="' + file_name + '"'
        response['Content-Length'] = file_size;
        #response['X-Sendfile'] = smart_str(file_path)        
        print(response['Content-Disposition']);
    except IOError:
        response = HttpResponseNotFound()
    print("response is", response)
    filesock.close()
    return response

def getUserAnnotation(user_id, annotation):
    #print("input annotation is ", annotation)
    if annotation:
        annotation_py = list(json.loads(annotation));
        annotation_py_final = list(json.loads(annotation));
        for annotation_itemm in annotation_py:
            if 'user_info' in annotation_itemm and annotation_itemm['user_info']:
                if int(annotation_itemm['user_info']['user_id']) != int(user_id):
                    annotation_py_final.remove(annotation_itemm);
            else:
                annotation_py_final.remove(annotation_itemm);
        #print("annotation_py now ", annotation_py_final);
        if annotation_py_final:
            return json.dumps(annotation_py_final)
    return None


# { darknet:
class ANNINFO(Structure):
    _fields_ = [
                ("x", c_int), 
                ("y", c_int), 
                ("w", c_int), 
                ("h", c_int), 
                ("pcClassName", c_char_p), 
                ("fCurrentFrameTimeStamp", c_double),
                ("nVideoId", c_int), 
                ("prob", c_double) 
               ]

RAISEANNFUNC = CFUNCTYPE(c_int, (ANNINFO))

class DETECTORMODEL(Structure):
    _fields_ = [("pcCfg", c_char_p),
                ("pcWeights", c_char_p),
                ("pcFileName", c_char_p),
                ("pcDataCfg", c_char_p),
                ("fTargetFps", c_double),
                ("fThresh", c_double),
                ("pfnRaiseAnnCb", (RAISEANNFUNC)),
                ("nVideoId", c_int),
                ("isVideo", c_int),
                ("nFrameId", c_int),
               ]

objects_raised = []

def raiseAnn(annInfo):
    #print("got annInfo x:" + annInfo.x + " y:" + annInfo.y + " w:" + annInfo.w + " h:" + annInfo.h 
    #    + " fCurrentFrameTimeStamp:" + annInfo.fCurrentFrameTimeStamp
    #    + " nVideoId:" + annInfo.nVideoId)
    x = int(annInfo.x)
    y = int(annInfo.y)
    w = int(annInfo.w)
    h = int(annInfo.h)
    fCurrentFrameTimeStamp = annInfo.fCurrentFrameTimeStamp
    
    print("got annInfo x:" + str(x) + " y:" + str(y) + " w:" + str(w) + " h:" + str(h) 
        + " fCurrentFrameTimeStamp:" + str(fCurrentFrameTimeStamp)
        )
    an_object = {}
    an_object['type'] = (annInfo.pcClassName).decode('utf-8')
    print("label " + an_object['type'])
    try:
        label = Label.objects.get(name__iexact=an_object['type'])
    except:
        print("unknown label " + an_object['type'])
        return 0
    an_object['keyframes'] = []
    box = {}
    box['bbID'] = -1;
    box['prob'] = annInfo.prob;
    box['continueInterpolation'] = False;
    box['x'] = annInfo.x
    box['y'] = annInfo.y
    box['w'] = annInfo.w
    box['h'] = annInfo.h
    box['frame'] = annInfo.fCurrentFrameTimeStamp / 1000
    an_object['keyframes'].append(box)
    if(label):
        color = label.color
    else:
        color = "#f28a9d";
    an_object['color'] = color
    user_info = {}
    user_info['user_id'] = "2"
    an_object['user_info'] = user_info
    objects_raised.append(an_object)
    return 0



def run_darknet_model(request, video_id, path, isVideo, frameID):
    lib = CDLL("/home/unnikrishnan/work/darknet/libdarknet.so", RTLD_LOCAL)
    import mimetypes
    import os.path
    mimetypes.init()
    print("request for running the model", request.user);
    try:
        cuser = User.objects.get(username=request.user);
        print("user is here ", cuser.id);
    except User.DoesNotExist:
        raise Http404('No user with name "{}"'.format(request.user))


    #TODO only admin can use this feature; 

    #now load YOLO and call the API with the local video path 
    # (TODO: add support for remote videos if required later)
    run_detector_model = lib.run_detector_model
    run_detector_model.argtypes = [POINTER(DETECTORMODEL)]
    run_detector_model.restype = c_int;

    detectorModel = DETECTORMODEL("/home/unnikrishnan/work/darknet/cfg/yolo.cfg".encode('utf-8'), 
        "/home/unnikrishnan/work/darknet/yolo.weights".encode('utf-8'), 
        #"/home/unnikrishnan/work/va/annotator/static/res/videos/1080p_WALSH_ST_000.mp4".encode('utf-8'),
        path.encode('utf-8'),
        "/home/unnikrishnan/work/darknet/cfg/coco.data".encode('utf-8'), 
        1, 
        0.24, 
        RAISEANNFUNC(raiseAnn), int(video_id),
        1 if isVideo == True else 0,
        frameID);
    detectorModel1 = DETECTORMODEL("/home/unnikrishnan/work/darknet/cfg/yolo.cfg".encode('utf-8'), "/home/unnikrishnan/work/darknet/yolo.weights".encode('utf-8'), 
        #"/home/unnikrishnan/work/va/annotator/static/res/videos/1080p_WALSH_ST_000.mp4".encode('utf-8'),
        "/home/unnikrishnan/work/va/annotator/static/res/image_list/1080p_WALSH_ST_0602_000/1080p_WALSH_ST_0602_000_00001.jpeg".encode('utf-8'),
        #"/Users/gotham/work/research/video_annotation_and_deeplearning/BeaverDam/annotator/static/res/videos/trimmed_walsh_night.mp4".encode('utf-8'),
        #"/Users/gotham/work//research/videos_demo/trimmed_walsh_night.mp4".encode('utf-8'),
        #"/Users/gotham/work/research/video_annotation_and_deeplearning/videos/test_darknet.mp4".encode('utf-8'),
        "/home/unnikrishnan/work/darknet/cfg/coco.data".encode('utf-8'), 
        1, 
        0.24, 
        RAISEANNFUNC(raiseAnn), 0, 
        0);

    #print("pcDataCfg is "  + detectorModel.pcDataCfg.decode("utf-8"));
    run_detector_model(pointer(detectorModel))

    """
    try:
        file_path = settings.BASE_DIR + '/' + 'dataset/video_' + video_id + '_' + str(cuser.id) + '.json';
 
        print("file_path:", file_path)
        filesock = open(file_path, 'r')
        file_name = os.path.basename(file_path)
        file_size = os.path.getsize(file_path)
        mime_type_guess = mimetypes.guess_type(file_name)
        print("mime", mime_type_guess, "file_size", file_size, "file_name", file_name);
        print(mime_type_guess)
        content = filesock.read()
        #print("content is ", content)
        if mime_type_guess is not None:
            response = HttpResponse(content, content_type=mime_type_guess[0])
        #response = HttpResponse(filesock, content_type='application/force-download')
        response['Content-Disposition'] = 'attachment; filename="' + file_name + '"'
        response['Content-Length'] = file_size;
        #response['X-Sendfile'] = smart_str(file_path)        
        print(response['Content-Disposition']);
    except IOError:
        response = HttpResponseNotFound()
    print("response is", response)
    filesock.close()
    """
    
    response = HttpResponse(status=200)
    return response
#} darknet

def run_rcnn_model(request, video_id, path, isVideo, frameID):
    
    print("request for running the model", request.user);
    try:
        cuser = User.objects.get(username=request.user);
        print("user is here ", cuser.id);
    except User.DoesNotExist:
        raise Http404('No user with name "{}"'.format(request.user))


    #TODO only admin can use this feature; 
    #TensorFlow Faster-RCNN Model
    print("hi tf")
    import sys
    sys.path.insert(0, '/home/unnikrishnan/work/tensorflow/lib/python3.5/site-packages/tensorflow/models/object_detection/')
    sys.path.insert(0, '/home/unnikrishnan/work/tensorflow/lib/python3.5/site-packages/tensorflow/models/')
    sys.path.insert(0, '/home/unnikrishnan/work/tensorflow/lib/python3.5/site-packages/tensorflow/models/slim/')

    print("hi tf")
    import json_output
    print("hi tf")
    json_output.load_models(path = '/home/unnikrishnan/work/tensorflow/lib/python3.5/site-packages/tensorflow/models/object_detection/')
    #list = json_output.get_recognize('/home/unnikrishnan/work/tensorflow/lib/python3.5/site-packages/tensorflow/models/object_detection/test_images/evaluate.png')
    #list = json_output.run_rcnn_on_vid('/home/unnikrishnan/work/va/annotator/static/res/videos/1080p_WALSH_ST_000.mp4')
    list = json_output.run_rcnn_on_vid(path, isVideo, frameID)
    
    for li in list:
        li_json=json.loads(li)
        ann=ANNINFO(int(li_json['x']), int(li_json['y']), int(li_json['w']), int(li_json['h']), li_json['type'].encode('utf-8'), li_json['frame'], int(li_json['video_id']), li_json['prob'])
        raiseAnn(ann)
    response = HttpResponse(status=200)
    return response

def get_video_or_image_list(request, video_id):
    try:
        video = Video.objects.get(id=video_id)
        labels = Label.objects.all()
    except Video.DoesNotExist:
        raise Http404('No video with id "{}". Possible fixes: \n1) Download an up to date DB, see README. \n2) Add this video to the DB via /admin'.format(video_id))
    response = HttpResponse(status=200)
    start_time = float(request.GET['s']) if 's' in request.GET else None
    end_time = float(request.GET['e']) if 'e' in request.GET else None
    video_data = json.dumps({
        'id': video.id,
        'location': video.url,
        'path': video.host,
        'is_image_sequence': True if video.image_list else False,
        'annotated': video.annotation != '',
        'verified': video.verified,
        'rejected': video.rejected,
        'start_time': start_time,
        'end_time' : end_time
    })
    print("image_list:" + video.image_list)
    cwd = os.getcwd()
    if(video.image_list):
        print("get_video_or_image_list image sequence")
        print("path is [" + video.host + "]")
        print("location is [" + video.url + "]")
        image_list_json = json.loads(video.image_list)
        print("IMAGE LISTTTTTT " + str(image_list_json))
        print("cwd: " + cwd)
        #NOTE: We assume all the file urls are w.r.t the static folder in annotator/ folder
        i = 0;
        for l in image_list_json:
            file = cwd + "/annotator/" + l
            print("[[[" + file + "]]]")
            #response = run_darknet_model(request, video_id, file, False, i)
            response = run_rcnn_model(request, video_id, file, False, i)
            i = i + 1
    else:
        print("path is " + video.filename)
        file = cwd + "/annotator/" + video.filename
        #response = run_darknet_model(request, video_id, file, True, i)
        response = run_rcnn_model(request, video_id, file, True, 0)
    return response
        

def run_model(request, video_id):
    global objects_raised
    objects_raised = []
    #response = run_darknet_model(request, video_id)
    #response = run_rcnn_model(request, video_id)
    response = get_video_or_image_list(request, video_id)
    video = Video.objects.get(id=video_id)
    video.annotation = json.dumps(objects_raised)
    video.save()
    return response
