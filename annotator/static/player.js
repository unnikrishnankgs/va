"use strict";


class Player {
    constructor({$container, videoSrc, videoId, videoStart, videoEnd, isImageSequence, turkMetadata, current_user}) {

        this.$container = $container;

        this.videoId = videoId;

        this.selectedAnnotation = null;

        this.annotations = null;

        this.annotationRectBindings = [];

        this.videoSrc = videoSrc;

        this.view = null;

        this.videoStart = videoStart;

        this.videoEnd = videoEnd;

        this.isImageSequence = isImageSequence;

        this.turkMetadata = turkMetadata;

        this.current_user = current_user;

        this.isImageSequence = isImageSequence;

        this.metrics = {
            playerStartTimes: Date.now(),
            annotationsStartTime: null,
            annotationsEndTime: null,
            browserAgent: navigator.userAgent
        };

        // Promises
        this.annotationsDataReady = Misc.CustomPromise();
        this.annotationsReady = Misc.CustomPromise();
        this.viewReady = Misc.CustomPromise();

        // We're ready when all the components are ready.
        this.ready = Misc.CustomPromiseAll(
            this.annotationsReady(),
            this.viewReady()
        );

        // Prevent adding new properties
        Misc.preventExtensions(this, Player);

        this.initAnnotations();
        this.initView();
        this.initHandlers();
    }


    // Init ALL the annotations!

    initView() {
        var {$container, videoSrc, videoStart, videoEnd} = this;

        this.view = new PlayerView({$container, videoSrc, videoStart, videoEnd});

        this.view.ready().then(this.viewReady.resolve);
    }

    initAnnotations() {
        DataSources.annotations.load(this.videoId).then((annotations) => {
            var annotations_final = annotations.slice();
            //window.alert(annotations.length);
            for(var i = 0; i < annotations.length; i++) {
                var annotation_item = annotations[i];
                if(annotation_item['user_info'] && (annotation_item['user_info']['user_id'] == this.current_user)) {
                    //window.alert("matched current_user" + this.current_user);
                }
                else {
                    //window.alert("matched current_user " + this.current_user + " with " + annotation_item['user_info']['user_id']);
                    /** delete the content which is not ours; basically the annotations made by some other user has
                     * to be deleted her
                     * hacky; but there is no problem with everyone seeing everyone else's annotations behind the hood
                     * - shouldn't be an issue even with Mturk as no 2 people can work on the same video individually
                     */
                    annotations_final.splice(annotations_final.indexOf(annotation_item), 1);
                }
            }
            //window.alert("annotations_final" + annotations_final);
            this.annotations = annotations_final;
            this.annotationsDataReady.resolve();
        });

        // When this.annotations is loaded AND view is ready for drawing...
        Promise.all([this.annotationsDataReady(), this.viewReady()]).then(() => {
            for (let annotation of this.annotations) {
                let rect = this.view.addRect();
                rect.fill = annotation.fill;
                this.initBindAnnotationAndRect(annotation, rect);
            }

            $(this).triggerHandler('change-onscreen-annotations');
            $(this).triggerHandler('change-keyframes');

            this.annotationsReady.resolve();
        });
    }

    initBindAnnotationAndRect(annotation, rect) {
        // On PlayerView...

        this.annotationRectBindings.push({annotation, rect});


        // On Rect...

        $(rect).on('discrete-change', (e, bounds) => {
            annotation.updateKeyframe({
                time: this.view.video.currentTime,
                bounds: bounds,
            }, this.isImageSequence);
            $(this).triggerHandler('change-onscreen-annotations');
            $(this).triggerHandler('change-keyframes');
        });

        $(rect).on('select', () => {
            this.selectedAnnotation = annotation;
            $(this).triggerHandler('change-keyframes');
        });

        $(rect).on('drag-start', () => {
            this.view.video.pause();
        });

        $(rect).on('focus', () => {
            this.selectedAnnotation = annotation;
            $(this).triggerHandler('change-onscreen-annotations');
            $(this).triggerHandler('change-keyframes');
        });


        // On Annotation...

        $(annotation).on('change delete', () => {
            rect.appear({singlekeyframe: annotation.keyframes.length === 1});
        });
        $(annotation).triggerHandler('change');

        $(annotation).on('delete', () => {
            $(annotation).off();
            $(rect).off();
            this.view.deleteRect(rect);
        });
    }

    initHandlers() {
        // Drawing annotations
        $(this).on('change-onscreen-annotations', () => {
            this.drawOnscreenAnnotations();
        });

        $(this).on('change-keyframes', () => {
            this.drawKeyframes();
        });


        // Submitting
        $('#submit-btn').click(this.submitAnnotations.bind(this));
        $('#download-btn').click(this.requestDownload.bind(this));

        $('#btn-show-accept').click(this.showAcceptDialog.bind(this));
        $('#btn-show-reject').click(this.showRejectDialog.bind(this));
        $('#btn-show-email').click(this.showEmailDialog.bind(this));

        $('#accept-reject-btn').click(this.acceptRejectAnnotations.bind(this));

        $('#email-btn').click(this.emailWorker.bind(this));

       
        // On drawing changed
        this.viewReady().then(() => {
            $(this.view.creationRect).on('drag-start', () => {
                this.view.video.pause();
            });

            $(this.view.creationRect).on('focus', () => {
               this.selectedAnnotation = null;
                $(this).triggerHandler('change-onscreen-annotations');
                $(this).triggerHandler('change-keyframes');
            });

            this.view.video.onTimeUpdate(() => {
                $(this).triggerHandler('change-onscreen-annotations');
            });

            $(this.view).on('create-rect', (e, rect) => {
                this.addAnnotationAtCurrentTimeFromRect(rect);
                rect.focus();
                $(this).triggerHandler('change-keyframes');
            });

            $(this.view).on('delete-keyframe', () => {
                this.view.video.pause();
                this.deleteSelectedKeyframe();
                $(this).triggerHandler('change-onscreen-annotations');
                $(this).triggerHandler('change-keyframes');
            });

            $(this.view).on('step-forward-keyframe', () => {
                var time = this.view.video.currentTime;
                if (!this.selectedAnnotation || !this.selectedAnnotation.keyframes)
                    return;
                for (let [i, kf] of this.selectedAnnotation.keyframes.entries()) {
                    if (Math.abs(time - kf.time) < this.selectedAnnotation.SAME_FRAME_THRESHOLD) {
                        if (i != this.selectedAnnotation.keyframes.length - 1) {
                            var nf = this.selectedAnnotation.keyframes[i + 1];
                            this.view.video.currentTime = nf.time;
                            break;
                        }
                    }
                }
            });

            $(this.view).on('step-backward-keyframe', () => {
                var time = this.view.video.currentTime;
                var selected = this.selectedAnnotation;
                if (!this.selectedAnnotation || !this.selectedAnnotation.keyframes)
                    return;
                for (let [i, kf] of this.selectedAnnotation.keyframes.entries()) {
                    if (Math.abs(time - kf.time) < this.selectedAnnotation.SAME_FRAME_THRESHOLD) {
                        if (i !== 0) {
                            var nf = this.selectedAnnotation.keyframes[i - 1];
                            this.view.video.currentTime = nf.time;
                            break;
                        }
                    }
                }
            });

        });
    }


    // Draw something

    drawOnscreenAnnotations() {
        for (let {annotation, rect} of this.annotationRectBindings) {
            this.drawAnnotationOnRect(annotation, rect);
        }
    }

    drawKeyframes() {
        this.view.keyframebar.resetWithDuration(this.view.video.duration);
        for (let annotation of this.annotations) {
            for (let keyframe of annotation.keyframes) {
                let selected = (annotation == this.selectedAnnotation);
                this.view.keyframebar.addKeyframeAt(keyframe.time, {selected});
            }
        }
    }

    drawAnnotationOnRect(annotation, rect) {
        if (this.metrics.annotationsStartTime == null) {
            this.metrics.annotationsStartTime = Date.now();
            // force the keyboard shortcuts to work within an iframe
            window.focus();
        }
        var time = this.view.video.currentTime;

        var {bounds, prevIndex, nextIndex, closestIndex, continueInterpolation} = annotation.getFrameAtTime(time, this.isImageSequence);

        // singlekeyframe determines whether we show or hide the object
        // we want to hide if:
        //   - the very first frame object is in the future (nextIndex == 0 && closestIndex is null)
        //   - we're after the last frame and that last frame was marked as continueInterpolation false
        rect.appear({
            real: closestIndex != null,
            selected: this.selectedAnnotation === annotation,
            singlekeyframe: continueInterpolation && !(nextIndex == 0 && closestIndex === null)
        });

        // Don't mess up our drag
        if (rect.isBeingDragged()) return;

        rect.bounds = bounds;
    }


    // Actions

    submitAnnotations(e) {
        e.preventDefault();
        this.metrics.annotationsEndTime = Date.now();
        if (this.metrics.annotationsStartTime == null) {
            this.metrics.annotationsStartTime = this.metrics.annotationsEndTime;
        }
        if (this.annotations.length === 0 && !confirm('Confirm that there are no objects in the video?')) {
            return;
        }
        DataSources.annotations.save(this.videoId, this.annotations, this.metrics, window.mturk).then((response) => {
            // only show this if not running on turk
            if (!window.hitId)
                this.showModal("Save", "Save Successful");
        });
    }

    requestDownload(e) {
    /**
        return fetch(`/download/${this.videoId}/`, {
                credentials: 'same-origin',
            }).then((response) =>{
                window.location = (response['Content-Disposition']);
            });
            **/

        /**
        var xhttp = new XMLHttpRequest();
        var dwld = `/download/${this.videoId}/`;
        xhttp.open("GET", dwld, true);
        xhttp.send();
        **/

        var anchor = document.createElement('a');
        //var dwld = (document.baseURI).slice(0, document.baseURI.indexOf("video"));// + `download/${this.videoId}/`;
        var file = `/download/${this.videoId}/`;
        anchor.href = document.location.origin + file;
        anchor.target = '_blank';
        anchor.download = "";
        anchor.click();
        
    }

    showModal(title, message) {
        $('#genericModalTitle')[0].innerText = title;
        $('#genericModalMessage')[0].innerText = message;
        $('#genericModal').modal();
    }

    showAcceptDialog(e) {
        this.setDialogDefaults();
        if (this.turkMetadata) {
            $('#inputAcceptRejectMessage')[0].value = this.turkMetadata.bonusMessage
        }
        $('#acceptRejectType')[0].value = 'accept';
        $('#labelForBonus').text("Bonus")
        $('#inputBonusAmt').prop('readonly', false);
        $('#inputReopen')[0].checked = false;
        $('#inputDeleteBoxes')[0].checked = false;
        $('#inputBlockWorker')[0].checked = false;
        $('#accept-reject-btn').removeClass('btn-danger').addClass('btn-success')
        $('#accept-reject-btn').text('Accept');
        $('#acceptRejectForm').find('.modal-title').text("Accept Work");
        $('#acceptRejectForm').modal('toggle'); 
    }
    showRejectDialog(e) {
        this.setDialogDefaults();
        if (this.turkMetadata) {
            $('#inputAcceptRejectMessage')[0].value = this.turkMetadata.rejectionMessage;
        }
        $('#acceptRejectType')[0].value = 'reject';
        $('#labelForBonus').text("Lost Bonus")
        $('#inputBonusAmt').prop('readonly', true);
        $('#inputReopen')[0].checked = true;
        $('#inputDeleteBoxes')[0].checked = true;
        $('#inputBlockWorker')[0].checked = false;
        $('#accept-reject-btn').removeClass('btn-success').addClass('btn-danger')
        $('#accept-reject-btn').text('Reject');
        $('#acceptRejectForm').find('.modal-title').text("Reject Work");
        $('#acceptRejectForm').modal('toggle');
    }
    setDialogDefaults(){
        if (this.turkMetadata) {
            $('#inputBonusAmt')[0].value = this.turkMetadata.bonus
            $('.workerTime').text(this.verbaliseTimeTaken(this.turkMetadata.storedMetrics));
            $('.readonlyBrowser').text(this.turkMetadata.storedMetrics.browserAgent);
        }
        else {
            $('.turkSpecific').css({display:'none'});
        }
    }

    showEmailDialog(e) {
        this.setDialogDefaults();
      
        $('#inputEmailMessage')[0].value = this.turkMetadata.emailMessage;
        $('#inputEmailSubject')[0].value = this.turkMetadata.emailSubject;
        $('#emailForm').modal('toggle');
    }

    verbaliseTimeTaken(metricsObj) {
        var timeInMillis = metricsObj.annotationsEndTime - metricsObj.annotationsStartTime;

        return Math.round(timeInMillis / 60 / 100) / 10 + " minutes";
    }

    acceptRejectAnnotations(e) {
        e.preventDefault();
        var bonus = $('#inputBonusAmt')[0];
        var message = $('#inputAcceptRejectMessage')[0];
        var reopen = $('#inputReopen')[0];
        var deleteBoxes = $('#inputDeleteBoxes')[0];
        var blockWorker = $('#inputBlockWorker')[0]
        var type = $('#acceptRejectType')[0].value;

        $('#acceptRejectForm').find('.btn').attr("disabled", "disabled");

        var promise;
        if (type == 'accept')
            promise = DataSources.annotations.acceptAnnotation(this.videoId, parseFloat(bonus.value), message.value, 
                                                               reopen.checked, deleteBoxes.checked, blockWorker.checked, this.annotations);
        else
            promise = DataSources.annotations.rejectAnnotation(this.videoId, message.value, reopen.checked, deleteBoxes.checked, blockWorker.checked, this.annotations);
            
        promise.then((response) => {
            $('#acceptForm').modal('toggle');
            $('#acceptForm').find('.btn').removeAttr("disabled");
            location.reload();
        }, (err) => {
            alert("There was an error processing your request.");
            $('#acceptForm').find('.btn').removeAttr("disabled");
        });
    }

    emailWorker(e) {
        e.preventDefault();
        var subject = $('#inputEmailSubject')[0];
        var message = $('#inputEmailMessage')[0];
        
        $('#emailForm').find('.btn').attr("disabled", "disabled");
        DataSources.annotations.emailWorker(this.videoId, subject.value, message.value).then((response) => {
            $('#emailForm').modal('toggle');
            $('#emailForm').find('.btn').removeAttr("disabled");
            location.reload();
        }, (err) => {
            alert("There was an error processing your request:\n" + err);
            $('#emailForm').find('.btn').removeAttr("disabled");
        });
    }

    addAnnotationAtCurrentTimeFromRect(rect) {
        var annotation = Annotation.newFromCreationRect(this.isImageSequence, true);
        annotation.updateKeyframe({
            time: this.view.video.currentTime,
            bounds: rect.bounds
        }, this.isImageSequence);
        this.annotations.push(annotation);
        rect.fill = annotation.fill;
        this.initBindAnnotationAndRect(annotation, rect);
    }

    deleteAnnotation(annotation) {
        if (annotation == null) return false;

        if (annotation == this.selectedAnnotation) {
            this.selectedAnnotation = null;
        }

        for (let i = 0; i < this.annotations.length; i++) {
            if (this.annotations[i] === annotation) {
                annotation.delete();
                this.annotations.splice(i, 1);
                this.annotationRectBindings.splice(i, 1);
                return true;
            }
        }

        throw new Error("Player.deleteAnnotation: annotation not found");
    }

    deleteSelectedKeyframe() {
        if (this.selectedAnnotation == null) return false;
        var selected = this.selectedAnnotation;
        this.selectedAnnotation = null;
        selected.deleteKeyframeAtTime(this.view.video.currentTime, this.isImageSequence);

        if (selected.keyframes.length === 0) {
            this.deleteAnnotation(selected);
        }

        return true;
    }
}

void Player;
