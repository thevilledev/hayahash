/*
 * hayahash64 and hayahash128 - small, fast, portable hash functions.
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

/* hayahash128(data, seed=0) -> tuple[int, int] */
static PyObject *
hayahash_hayahash128(PyObject *self, PyObject *args, PyObject *kwargs)
{
	Py_buffer view;
	PyObject *data_obj;
	PyObject *seed_obj = NULL;
	uint64_t seed;
	hayahash128_t digest;
	static char *kwlist[] = {"data", "seed", NULL};

	(void)self;
	if (!PyArg_ParseTupleAndKeywords(args, kwargs, "O|O:hayahash128",
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
			"hayahash128() requires a C-contiguous buffer");
		return NULL;
	}

	digest = hayahash128(view.buf, (ptrdiff_t)view.len, seed);
	PyBuffer_Release(&view);
	return Py_BuildValue("(KK)",
		(unsigned long long)digest.lo,
		(unsigned long long)digest.hi);
}

/*
 * Hasher: incremental hashing over the reference streaming state.
 *
 * The digest equals hayahash64()/hayahash128() of the concatenation of
 * every update(), for any split. Finalizing does not consume the state.
 */
typedef struct {
	PyObject_HEAD
	hayahash64_state st;
	uint64_t seed;
} HasherObject;

static PyTypeObject Hasher_Type;

static PyObject *
Hasher_new(PyTypeObject *type, PyObject *args, PyObject *kwargs)
{
	HasherObject *self;
	PyObject *seed_obj = NULL;
	uint64_t seed;
	static char *kwlist[] = {"seed", NULL};

	if (!PyArg_ParseTupleAndKeywords(args, kwargs, "|O:Hasher",
			kwlist, &seed_obj)) {
		return NULL;
	}
	if (parse_seed(seed_obj, &seed) < 0) {
		return NULL;
	}
	self = (HasherObject *)type->tp_alloc(type, 0);
	if (self == NULL) {
		return NULL;
	}
	self->seed = seed;
	hayahash64_init(&self->st, seed);
	return (PyObject *)self;
}

static void
Hasher_dealloc(HasherObject *self)
{
	PyTypeObject *tp = Py_TYPE(self);
	tp->tp_free((PyObject *)self);
}

static PyObject *
Hasher_update(HasherObject *self, PyObject *data_obj)
{
	Py_buffer view;

	if (PyObject_GetBuffer(data_obj, &view, PyBUF_SIMPLE) < 0) {
		return NULL;
	}
	if (!PyBuffer_IsContiguous(&view, 'C')) {
		PyBuffer_Release(&view);
		PyErr_SetString(PyExc_ValueError,
			"update() requires a C-contiguous buffer");
		return NULL;
	}
	hayahash64_update(&self->st, view.buf, (size_t)view.len);
	PyBuffer_Release(&view);
	Py_RETURN_NONE;
}

static PyObject *
Hasher_digest64(HasherObject *self, PyObject *Py_UNUSED(ignored))
{
	return PyLong_FromUnsignedLongLong(
		(unsigned long long)hayahash64_digest(&self->st));
}

static PyObject *
Hasher_digest128(HasherObject *self, PyObject *Py_UNUSED(ignored))
{
	hayahash128_t digest = hayahash128_digest(&self->st);
	return Py_BuildValue("(KK)",
		(unsigned long long)digest.lo,
		(unsigned long long)digest.hi);
}

static PyObject *
Hasher_copy(HasherObject *self, PyObject *Py_UNUSED(ignored))
{
	HasherObject *copy;

	copy = PyObject_New(HasherObject, &Hasher_Type);
	if (copy == NULL) {
		return NULL;
	}
	copy->st = self->st;
	copy->seed = self->seed;
	return (PyObject *)copy;
}

static PyObject *
Hasher_reset(HasherObject *self, PyObject *args, PyObject *kwargs)
{
	PyObject *seed_obj = NULL;
	uint64_t seed;
	static char *kwlist[] = {"seed", NULL};

	if (!PyArg_ParseTupleAndKeywords(args, kwargs, "|O:reset",
			kwlist, &seed_obj)) {
		return NULL;
	}
	/* No seed given: restart with the one this hasher was built with. */
	if (seed_obj == NULL || seed_obj == Py_None) {
		seed = self->seed;
	} else if (parse_seed(seed_obj, &seed) < 0) {
		return NULL;
	}
	self->seed = seed;
	hayahash64_init(&self->st, seed);
	Py_RETURN_NONE;
}

static PyObject *
Hasher_get_seed(HasherObject *self, void *Py_UNUSED(closure))
{
	return PyLong_FromUnsignedLongLong((unsigned long long)self->seed);
}

static PyObject *
Hasher_get_length(HasherObject *self, void *Py_UNUSED(closure))
{
	return PyLong_FromUnsignedLongLong(
		(unsigned long long)self->st.total);
}

static PyObject *
Hasher_repr(HasherObject *self)
{
	return PyUnicode_FromFormat("<hayahash.Hasher seed=%llu length=%llu>",
		(unsigned long long)self->seed,
		(unsigned long long)self->st.total);
}

static PyMethodDef Hasher_methods[] = {
	{
		"update",
		(PyCFunction)Hasher_update,
		METH_O,
		"update(data) -> None\n"
		"\n"
		"Absorb bytes-like data. The digest is the same for every\n"
		"split of the same total input.",
	},
	{
		"digest64",
		(PyCFunction)Hasher_digest64,
		METH_NOARGS,
		"digest64() -> int\n"
		"\n"
		"Return the 64-bit digest of everything absorbed so far.\n"
		"Does not consume the state, so updating may continue.",
	},
	{
		"digest128",
		(PyCFunction)Hasher_digest128,
		METH_NOARGS,
		"digest128() -> tuple[int, int]\n"
		"\n"
		"Return both digest words. The low word is exactly\n"
		"digest64(). Does not consume the state.",
	},
	{
		"copy",
		(PyCFunction)Hasher_copy,
		METH_NOARGS,
		"copy() -> Hasher\n"
		"\n"
		"Return an independent hasher with the same absorbed state.",
	},
	{
		"reset",
		(PyCFunction)(void (*)(void))Hasher_reset,
		METH_VARARGS | METH_KEYWORDS,
		"reset(seed=None) -> None\n"
		"\n"
		"Discard absorbed input. Keeps the current seed unless a new\n"
		"one is given.",
	},
	{NULL, NULL, 0, NULL}
};

static PyGetSetDef Hasher_getset[] = {
	{"seed", (getter)Hasher_get_seed, NULL, "The 64-bit seed.", NULL},
	{"length", (getter)Hasher_get_length, NULL,
		"Number of bytes absorbed so far.", NULL},
	{NULL, NULL, NULL, NULL, NULL}
};

static PyTypeObject Hasher_Type = {
	PyVarObject_HEAD_INIT(NULL, 0)
	.tp_name = "hayahash._hayahash.Hasher",
	.tp_basicsize = sizeof(HasherObject),
	.tp_dealloc = (destructor)Hasher_dealloc,
	.tp_repr = (reprfunc)Hasher_repr,
	.tp_flags = Py_TPFLAGS_DEFAULT,
	.tp_doc = "Hasher(seed=0)\n"
		"\n"
		"Incremental hayahash state. update() absorbs bytes-like data;\n"
		"digest64() and digest128() return the digest of everything\n"
		"absorbed so far without consuming the state. The result is\n"
		"identical to hayahash64()/hayahash128() over the concatenated\n"
		"input, for every split.\n"
		"\n"
		"Not safe for concurrent use from multiple threads.",
	.tp_methods = Hasher_methods,
	.tp_getset = Hasher_getset,
	.tp_new = Hasher_new,
};

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
	{
		"hayahash128",
		(PyCFunction)(void (*)(void))hayahash_hayahash128,
		METH_VARARGS | METH_KEYWORDS,
		"hayahash128(data, seed=0) -> tuple[int, int]\n"
		"\n"
		"Hash bytes-like data with an optional 64-bit seed and return\n"
		"the low and high words. The low word is exactly hayahash64().",
	},
	{NULL, NULL, 0, NULL}
};

static struct PyModuleDef hayahash_module = {
	PyModuleDef_HEAD_INIT,
	.m_name = "hayahash._hayahash",
	.m_doc = "C extension binding for hayahash64 and hayahash128.",
	.m_size = -1,
	.m_methods = hayahash_methods,
};

PyMODINIT_FUNC
PyInit__hayahash(void)
{
	PyObject *module;

	if (PyType_Ready(&Hasher_Type) < 0) {
		return NULL;
	}
	module = PyModule_Create(&hayahash_module);
	if (module == NULL) {
		return NULL;
	}
	Py_INCREF(&Hasher_Type);
	if (PyModule_AddObject(module, "Hasher",
			(PyObject *)&Hasher_Type) < 0) {
		Py_DECREF(&Hasher_Type);
		Py_DECREF(module);
		return NULL;
	}
	return module;
}
