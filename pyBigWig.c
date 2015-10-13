//Some versions of GCC optimize the functions in a way that segfaults
#pragma GCC optimize("O0")
#include <Python.h>
#include <assert.h>
#include <inttypes.h>
#include "pyBigWig.h"

//Need to add proper error handling rather than just assert()
PyObject* pyBwOpen(PyObject *self, PyObject *pyFname) {
    char *fname = NULL;
    pyBigWigFile_t *pybw;
    bigWigFile_t *bw = NULL;

    if(!PyArg_ParseTuple(pyFname, "s", &fname)) goto error;

    //Open the local/remote file
    bw = bwOpen(fname, NULL);
    if(!bw) goto error;

    //Convert the chromosome list into a python hash
    if(!bw->cl) goto error;

    pybw = PyObject_New(pyBigWigFile_t, &bigWigFile);
    if(!pybw) goto error;
    pybw->bw = bw;
    PyObject_GC_Init((PyObject*) pybw);
    return (PyObject*) pybw;

error:
    bwClose(bw);
    Py_INCREF(Py_None);
    return Py_None;
}

static void pyBwDealloc(pyBigWigFile_t *self) {
    PyObject_GC_Fini((PyObject*) self);
    if(self->bw) bwClose(self->bw);
    PyObject_DEL(PyObject_AS_GC(self));
}

static PyObject *pyBwClose(pyBigWigFile_t *self, PyObject *args) {
    bwClose(self->bw);
    self->bw = NULL;
    Py_INCREF(Py_None);
    return Py_None;
}

//Accessor for the header (version, nLevels, nBasesCovered, minVal, maxVal, sumData, sumSquared
static PyObject *pyBwGetHeader(pyBigWigFile_t *self, PyObject *args) {
    bigWigFile_t *bw = self->bw;
    PyObject *ret, *val;

    ret = PyDict_New();
    val = PyLong_FromUnsignedLong(bw->hdr->version);
    if(PyDict_SetItemString(ret, "version", val) == -1) goto error;
    Py_DECREF(val);
    val = PyLong_FromUnsignedLong(bw->hdr->nLevels);
    if(PyDict_SetItemString(ret, "nLevels", val) == -1) goto error;
    Py_DECREF(val);
    val = PyLong_FromUnsignedLongLong(bw->hdr->nBasesCovered);
    if(PyDict_SetItemString(ret, "nBasesCovered", val) == -1) goto error;
    Py_DECREF(val);
    val = PyLong_FromDouble(bw->hdr->minVal);
    Py_DECREF(val);
    val = PyLong_FromDouble(bw->hdr->minVal);
    if(PyDict_SetItemString(ret, "minVal", val) == -1) goto error;
    Py_DECREF(val);
    val = PyLong_FromDouble(bw->hdr->maxVal);
    if(PyDict_SetItemString(ret, "maxVal", val) == -1) goto error;
    Py_DECREF(val);
    val = PyLong_FromDouble(bw->hdr->sumData);
    if(PyDict_SetItemString(ret, "sumData", val) == -1) goto error;
    Py_DECREF(val);
    val = PyLong_FromDouble(bw->hdr->sumSquared);
    if(PyDict_SetItemString(ret, "sumSquared", val) == -1) goto error;
    Py_DECREF(val);

    Py_INCREF(ret);
    return ret;

error :
    Py_XDECREF(val);
    Py_XDECREF(ret);
    Py_INCREF(Py_None);
    return Py_None;
}

//Accessor for the chroms, args is optional
static PyObject *pyBwGetChroms(pyBigWigFile_t *self, PyObject *args) {
    PyObject *ret = Py_None, *val;
    bigWigFile_t *bw = self->bw;
    char *chrom = NULL;
    uint32_t i;

    if(!(PyArg_ParseTuple(args, "|s", &chrom)) || !chrom) {
        ret = PyDict_New();
        for(i=0; i<bw->cl->nKeys; i++) {
            val = PyLong_FromUnsignedLong(bw->cl->len[i]);
            if(PyDict_SetItemString(ret, bw->cl->chrom[i], val) == -1) goto error;
            Py_DECREF(val);
        }
    } else {
        for(i=0; i<bw->cl->nKeys; i++) {
            if(strcmp(bw->cl->chrom[i],chrom) == 0) {
                ret = PyLong_FromUnsignedLong(bw->cl->len[i]);
                break;
            }
        }
    }

    Py_INCREF(ret);
    return ret;

error :
    Py_XDECREF(val);
    Py_XDECREF(ret);
    Py_INCREF(Py_None);
    return Py_None;
}

enum bwStatsType char2enum(char *s) {
    if(strcmp(s, "mean") == 0) return mean;
    if(strcmp(s, "std") == 0) return std;
    if(strcmp(s, "dev") == 0) return dev;
    if(strcmp(s, "max") == 0) return max;
    if(strcmp(s, "min") == 0) return min;
    if(strcmp(s, "cov") == 0) return cov;
    if(strcmp(s, "coverage") == 0) return cov;
    return -1;
};

//Fetch summary statistics, default is the mean of the entire chromosome.
static PyObject *pyBwGetStats(pyBigWigFile_t *self, PyObject *args, PyObject *kwds) {
    bigWigFile_t *bw = self->bw;
    double *val;
    uint32_t start = 0, end = -1, tid;
    static char *kwd_list[] = {"chrom", "start", "end", "type", "nBins", NULL};
    char *chrom, *type = "mean";
    PyObject *ret;
    int i, nBins = 1;

    if(!PyArg_ParseTupleAndKeywords(args, kwds, "s|kksi", kwd_list, &chrom, &start, &end, &type, &nBins)) {
        Py_INCREF(Py_None);
        return Py_None;
    }

    //Check inputs, reset to defaults if nothing was input
    if(!nBins) nBins = 1; //For some reason, not specifying this overrides the default!
    if(!type) type = "mean";
    tid = bwGetTid(bw, chrom);
    if(tid == -1) {
        Py_INCREF(Py_None);
        return Py_None;
    }
    if(end <= start || end > bw->cl->len[tid]) end = bw->cl->len[tid];
    if(start >= end) start = end-1;

    if(char2enum(type) == doesNotExist) {
        Py_INCREF(Py_None);
        return Py_None;
    }

    //Get the actual statistics
    val = bwStats(bw, chrom, start, end, nBins, char2enum(type));
    if(!val) {
        Py_INCREF(Py_None);
        return Py_None;
    }

    ret = PyList_New(nBins);
    for(i=0; i<nBins; i++) PyList_SetItem(ret, i, (isnan(val[i]))?Py_None:PyFloat_FromDouble(val[i]));
    free(val);

    Py_INCREF(ret);
    return ret;
}

//Fetch a list of individual values
//For bases with no coverage, the value should be None
static PyObject *pyBwGetValues(pyBigWigFile_t *self, PyObject *args) {
    bigWigFile_t *bw = self->bw;
    int i;
    uint32_t start, end, tid;
    char *chrom;
    PyObject *ret;
    bwOverlappingIntervals_t *o;

    if(!PyArg_ParseTuple(args, "skk", &chrom, &start, &end)) {
        Py_INCREF(Py_None);
        return Py_None;
    }

    tid = bwGetTid(bw, chrom);
    if(tid == -1) {
        Py_INCREF(Py_None);
        return Py_None;
    }
    if(end <= start || end > bw->cl->len[tid]) end = bw->cl->len[tid];
    if(start >= end) start = end-1;

    o = bwGetValues(self->bw, chrom, start, end, 1);
    if(!o) {
        Py_INCREF(Py_None);
        return Py_None;
    }

    ret = PyList_New(end-start);
    for(i=0; i<o->l; i++) PyList_SetItem(ret, i, PyFloat_FromDouble(o->value[i]));
    bwDestroyOverlappingIntervals(o);

    Py_INCREF(ret);
    return ret;
}

static PyObject *pyBwGetIntervals(pyBigWigFile_t *self, PyObject *args, PyObject *kwds) {
    bigWigFile_t *bw = self->bw;
    uint32_t start = 0, end = -1, tid, i;
    static char *kwd_list[] = {"chrom", "start", "end", NULL};
    bwOverlappingIntervals_t *intervals = NULL;
    char *chrom;
    PyObject *ret;

    if(!PyArg_ParseTupleAndKeywords(args, kwds, "s|kk", kwd_list, &chrom, &start, &end)) {
        Py_INCREF(Py_None);
        return Py_None;
    }

    //Sanity check
    tid = bwGetTid(bw, chrom);
    if(tid == -1) {
        Py_INCREF(Py_None);
        return Py_None;
    }
    if(end <= start || end > bw->cl->len[tid]) end = bw->cl->len[tid];
    if(start >= end) start = end-1;

    //Get the intervals
    intervals = bwGetOverlappingIntervals(bw, chrom, start, end);
    if(!intervals || !intervals->l) {
        Py_INCREF(Py_None);
        return Py_None;
    }

    ret = PyTuple_New(intervals->l);
    for(i=0; i<intervals->l; i++) {
        if(PyTuple_SetItem(ret, i, Py_BuildValue("(iif)", intervals->start[i], intervals->end[i], intervals->value[i]))) {
            Py_DECREF(ret);
            bwDestroyOverlappingIntervals(intervals);
            Py_INCREF(Py_None);
            return Py_None;
        }
    }

    bwDestroyOverlappingIntervals(intervals);
    Py_INCREF(ret);
    return ret;
}

PyMODINIT_FUNC initpyBigWig(void) {
    errno=0; //Sometimes libpython2.7.so is missing some links...
    if(PyType_Ready(&bigWigFile) < 0) return;
    if(bwInit(128000)) return; //This is temporary
    Py_InitModule3("pyBigWig", bwMethods, "A module for handling bigWig files");
}
