/*
 * hayahash64 - small, fast, portable 64-bit hash function.
 *
 * Python C extension wrapping the reference implementation in
 * hayahash.h. This is free and unencumbered software released into
 * the public domain. For more information, please refer to
 * https://unlicense.org/
 */

#define PY_SSIZE_T_CLEAN
#include <Python.h>

#include "hayahash.h"

#include <stdint.h>

static int
parse_seed(PyObject *seed_obj, uint64_t *seed_out)
{
	unsigned long long value;

	if (seed_obj == NULL || seed_obj == Py_None) {
		*seed_out = 0;
		return 0;
	}
	value = PyLong_AsUnsignedLongLong(seed_obj);
	if (value == (unsigned long long)-1 && PyErr_Occurred()) {
		return -1;
	}
	*seed_out = (uint64_t)value;
	return 0;
}

/* hayahash64(data, seed=0) -> int */
static PyObject *
hayahash_hayahash64(PyObject *self, PyObject *args, PyObject *kwargs)
{
	Py_buffer view;
	PyObject *data_obj;
	PyObject *seed_obj = NULL;
	uint64_t seed;
	uint64_t digest;
	static char *kwlist[] = {"data", "seed", NULL};

	(void)self;
	if (!PyArg_ParseTupleAndKeywords(args, kwargs, "O|O:hayahash64",
			kwlist, &data_obj, &seed_obj)) {
		return NULL;
	}
	if (parse_seed(seed_obj, &seed) < 0) {
		return NULL;
	}
	if (PyObject_GetBuffer(data_obj, &view, PyBUF_SIMPLE) < 0) {
		return NULL;
	}
	if (!PyBuffer_IsContiguous(&view, 'C')) {
		PyBuffer_Release(&view);
		PyErr_SetString(PyExc_ValueError,
			"hayahash64() requires a C-contiguous buffer");
		return NULL;
	}

	digest = hayahash64(view.buf, (ptrdiff_t)view.len, seed);
	PyBuffer_Release(&view);
	return PyLong_FromUnsignedLongLong((unsigned long long)digest);
}

static PyMethodDef hayahash_methods[] = {
	{
		"hayahash64",
		/* METH_KEYWORDS functions take three args; cast via void(*)(void)
		   is the portable pattern used throughout CPython itself. */
		(PyCFunction)(void (*)(void))hayahash_hayahash64,
		METH_VARARGS | METH_KEYWORDS,
		"hayahash64(data, seed=0) -> int\n"
		"\n"
		"Hash bytes-like data with an optional 64-bit seed and return\n"
		"the unsigned 64-bit digest as a Python int. Bit-exact with\n"
		"the C reference hayahash64().",
	},
	{NULL, NULL, 0, NULL}
};

static struct PyModuleDef hayahash_module = {
	PyModuleDef_HEAD_INIT,
	.m_name = "hayahash._hayahash",
	.m_doc = "C extension binding for hayahash64.",
	.m_size = -1,
	.m_methods = hayahash_methods,
};

PyMODINIT_FUNC
PyInit__hayahash(void)
{
	return PyModule_Create(&hayahash_module);
}
