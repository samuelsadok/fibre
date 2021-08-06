
//import { fileURLToPath } from 'url';
//import { dirname } from 'path';
//const __dirname = dirname(fileURLToPath(import.meta.url));

import Module from './libfibre-wasm.js';
import wasm from './libfibre-wasm.js';

function assert(expression) {
    if (!expression) {
        throw Error("assert failed");
    }
}

const FIBRE_STATUS_OK = 0;
const FIBRE_STATUS_BUSY = 1;
const FIBRE_STATUS_CANCELLED = 2;
const FIBRE_STATUS_CLOSED = 3;
const FIBRE_STATUS_INVALID_ARGUMENT = 4;
const FIBRE_STATUS_INTERNAL_ERROR = 5;
const FIBRE_STATUS_PROTOCOL_ERROR = 6;
const FIBRE_STATUS_HOST_UNREACHABLE = 7;

const FIBRE_CALL_TYPE_START_CALL = 0;
const FIBRE_CALL_TYPE_WRITE = 1;
const FIBRE_CALL_TYPE_WRITE_DONE = 2;

const ptrSize = 4;

class LibFibreChunk {
    constructor(ptr) { this.ptr = ptr; }
    get layer() { return this.ptr.getUint8(0); }
    set layer(val) { this.ptr.setUint8(0, val); }
    get begin() { return this.ptr.getUint32(4); }
    set begin(val) { this.ptr.setUint32(4, val); }
    get end() { return this.ptr.getUint32(8); }
    set end(val) { this.ptr.setUint32(8, val); }

    isFrameBoundary() { return this.end - this.begin == 0xffffffff; }
}
LibFibreChunk.size = 12;

class LibFibreTask {
    constructor(ptr) { this.ptr = ptr; }
    get type() { return this.ptr.getUint32(0); }
    set type(val) { this.ptr.setUint32(0, val); }
    get handle() { return this.ptr.getUint32(4); }
    set handle(val) { this.ptr.setUint32(4, val); }
    get startCall() { return new Pointer(this.ptr.buffer, LibFibreStartCallTask, this.ptr.addr + 8).deref(); }
    get write() { return new Pointer(this.ptr.buffer, LibFibreWriteTask, this.ptr.addr + 8).deref(); }
    get writeDone() { return new Pointer(this.ptr.buffer, LibFibreWriteDoneTask, this.ptr.addr + 8).deref(); }
}
LibFibreTask.size = 7 * 4;

class LibFibreStartCallTask {
    constructor(ptr) { this.ptr = ptr; }
    get func() { return this.ptr.getUint32(0); }
    set func(val) { this.ptr.setUint32(0, val); }
    get domain() { return this.ptr.getUint32(4); }
    set domain(val) { this.ptr.setUint32(4, val); }
}

class LibFibreWriteTask {
    constructor(ptr) { this.ptr = ptr; }
    get bBegin() { return this.ptr.getUint32(0); }
    set bBegin(val) { this.ptr.setUint32(0, val); }
    get cBegin() { return this.ptr.getUint32(4); }
    set cBegin(val) { this.ptr.setUint32(4, val); }
    get cEnd() { return this.ptr.getUint32(8); }
    set cEnd(val) { this.ptr.setUint32(8, val); }
    get elevation() { return this.ptr.getUint8(12); }
    set elevation(val) { this.ptr.setUint8(12, val); }
    get status() { return this.ptr.getInt32(16); }
    set status(val) { this.ptr.setInt32(16, val); }
}

class LibFibreWriteDoneTask {
    constructor(ptr) { this.ptr = ptr; }
    get status() { return this.ptr.getInt32(0); }
    set status(val) { this.ptr.setInt32(0, val); }
    get cEnd() { return this.ptr.getUint32(4); }
    set cEnd(val) { this.ptr.setUint32(4, val); }
    get bEnd() { return this.ptr.getUint32(8); }
    set bEnd(val) { this.ptr.setUint32(8, val); }
}



function _getError(code) {
    if (code == FIBRE_STATUS_CANCELLED) {
        return Error("libfibre: Operation Cancelled");
    } else if (code == FIBRE_STATUS_CLOSED) {
        return Error("libfibre: Closed");
    } else if (code == FIBRE_STATUS_INVALID_ARGUMENT) {
        return Error("libfibre: Invalid Argument");
    } else if (code == FIBRE_STATUS_INTERNAL_ERROR) {
        return Error("libfibre: Internal Error");
    } else if (code == FIBRE_STATUS_PROTOCOL_ERROR) {
        return Error("libfibre: Misbehaving Peer");
    } else if (code == FIBRE_STATUS_HOST_UNREACHABLE) {
        return Error("libfibre: Host Unreachable");
    } else {
        return Error("libfibre: Unknown Error " + code);
    }
}

class BasicCodec {
    constructor(typeName, byteLength, littleEndian) {
        this.getSize = () => byteLength;
        this.serialize = (libfibre, val, ptr) => {
            new DataView(libfibre.wasm.Module.HEAPU8.buffer, ptr)['set' + typeName](0, val, littleEndian);
            return byteLength;
        }
        this.deserialize = (libfibre, arr) => {
            return new DataView(arr.buffer, arr.byteOffset, arr.byteLength)['get' + typeName](0, littleEndian);
        }
    }
}

class BoolCodec {
    constructor() {
        this.getSize = () => 1;
        this.serialize = (libfibre, val, ptr) => {
            val = (val) ? 1 : 0;
            new DataView(libfibre.wasm.Module.HEAPU8.buffer, ptr).setUint8(0, val._handle);
            return 1;
        }
        this.deserialize = (libfibre, arr) => {
            return !!(new DataView(arr.buffer, arr.byteOffset, arr.byteLength).getUint8(0));
        }
    }
}

class ObjectPtrCodec {
    constructor() {
        this.getSize = () => 4;
        this.serialize = (libfibre, val, ptr) => {
            if (!val._handle) {
                throw Error("attempt to serialize stale object");
            }
            new DataView(libfibre.wasm.Module.HEAPU8.buffer, ptr).setUint32(0, val._handle, true);
            return 4;
        }
        this.deserialize = (libfibre, arr) => {
            const handle = new DataView(arr.buffer, arr.byteOffset, arr.byteLength).getUint32(0, true);
            return libfibre._objMap[handle]; // TODO: this can fail in some cases where the attribute was not previously fetched
        }
    }
}

const codecs = {
    'int8': new BasicCodec('Int8', 1, true),
    'uint8': new BasicCodec('Uint8', 1, true),
    'int16': new BasicCodec('Int16', 2, true),
    'uint16': new BasicCodec('Uint16', 2, true),
    'int32': new BasicCodec('Int32', 4, true),
    'uint32': new BasicCodec('Uint32', 4, true),
    'int64': new BasicCodec('BigInt64', 8, true),
    'uint64': new BasicCodec('BigUint64', 8, true),
    'float': new BasicCodec('Float32', 4, true),
    'bool': new BoolCodec(),
    'object_ref': new ObjectPtrCodec(),
};

class EofError extends Error {
    constructor(msg) {
        super(msg);
    }
}

class TxSocket {
    constructor(call, handle) {
        this._call = call;
        this._handle = handle;
        this._bBegin = 0;
        this._cBegin = 0;
        this._cEnd = 0;
    }

    async writeAll(chunkStart, chunkEnd, status) {
        assert(this._completer == undefined);

        this._status = status;
        this._bBegin = chunkStart.addr == chunkEnd.addr ? 0 : chunkStart.deref().begin;
        this._cBegin = chunkStart.addr;
        this._cEnd = chunkEnd.addr;

        this._enqueueRemainingChunks();
        
        return new Promise((resolve, reject) => this._completer = {resolve: resolve, reject: reject});
    }

    _enqueueRemainingChunks() {
        this._call._libfibre.enqueueTask((task) => {
            task.type = FIBRE_CALL_TYPE_WRITE;
            task.handle = this._handle;
            task.write.bBegin = this._bBegin;
            task.write.cBegin = this._cBegin;
            task.write.cEnd = this._cEnd;
            task.write.elevation = 0;
            task.write.status = this._status;
        });
    }

    _onWriteDone(result) {
        assert(result.bEnd == 0);
        this._bBegin = result.bEnd;
        this._cBegin = result.cEnd;

        if (this._cBegin == this._cEnd && result.status == this._status) {
            this._completer.resolve(true);
        } else if (result.status != FIBRE_STATUS_OK) {
            this._completer.reject(result.status);
        } else {
            this._enqueueRemainingChunks();
        }

        if (result.status != FIBRE_STATUS_OK) {
            this._call.closeHalf(1);
        }
    }
}

class ArgCollector {
    constructor(call, handle, argsDesc) {
        this._call = call;
        this._handle = handle;
        this._argDesc = argsDesc;
        this._pos = 0;
        this._queue = [];
        this._advanceQueue();
    }

    _closeArg() {
        this._completer.resolve({done: false, value: this._argDesc[this._pos].codec.deserialize(this._call._libfibre, this._lastArg)})
    }

    _advanceQueue() {
        this._queue.push(new Promise((resolve, reject) => this._completer = {resolve: resolve, reject: reject}));
        this._lastArg = new Uint8Array();
    }
    
    [Symbol.asyncIterator]() {
        return {
            parent: this,
            async next() {
                const val = await this.parent._queue[0];
                this.parent._queue = this.parent._queue.slice(1);
                return val;
            }
        };
    }
    
    _onWrite(args) {
        var chunkPtr = new Pointer(this._call._libfibre.wasm.Module.HEAPU8.buffer, LibFibreChunk, args.cBegin);
        while (chunkPtr.addr != args.cEnd) {
            const chunk = chunkPtr.deref();
            assert(chunk.layer == 0);

            if (chunk.isFrameBoundary()) {
                this._closeArg();
                this._advanceQueue();
            } else {
                this._lastArg = new Uint8Array([...this._lastArg, ...this._call._libfibre.wasm.Module.HEAPU8.subarray(chunk.begin, chunk.end)]);
            }

            chunkPtr = chunkPtr.add(1);
        }

        if (args.status != FIBRE_STATUS_OK) {
            if (args.status == FIBRE_STATUS_CLOSED) {
                this._completer.resolve({done: true});
            } else {
                this._completer.reject(args.status);
            }
        }

        this._call._libfibre.enqueueTask((task) => {
            task.type = FIBRE_CALL_TYPE_WRITE_DONE;
            task.handle = this._handle;
            task.writeDone.status = args.status;
            task.writeDone.cEnd = chunkPtr.addr;
            task.writeDone.bEnd = 0;
        });


        if (args.status != FIBRE_STATUS_OK) {
            this._call.closeHalf(0);
        }
    }
};


export class RemoteInterface {
    constructor(libfibre, intfName, handle) {
        this._libfibre = libfibre;
        this._refCount = 0;
        this._children = [];
        this._intfName = intfName;
        this._intfHandle = handle;
        this._onLost = new Promise((resolve) => this._onLostResolve = resolve);
    }
}

const _staleObject = {};

export class Call {
    constructor(libfibre, outArgDesc) {
        this._libfibre = libfibre;
        this._id = libfibre._allocRef(this);
        this.txSocket = new TxSocket(this, this._id);
        this.argCollector = new ArgCollector(this, this._id, outArgDesc);
    }

    closeHalf(idx) {
        if (idx == 0) {
            delete this.argCollector;
        } else if (idx == 1) {
            delete this.txSocket;
        }
        if (this.argCollector == undefined && this.txSocket == undefined) {
            this._libfibre._freeRef(this._id);
        }
    }
}

class Pointer {
    constructor(buffer, type, addr) {
        this.buffer = buffer;
        this.type = type;
        this.addr = addr;
    }
    add(offset) { return new Pointer(this.buffer, this.type, this.addr + offset * this.type.size); }
    deref() { return new this.type(this); }
    getUint8(offset) { return new Uint8Array(this.buffer, this.addr + offset)[0]; }
    setUint8(offset, val) { new Uint8Array(this.buffer, this.addr + offset)[0] = val; }
    getInt32(offset) { return new Int32Array(this.buffer, this.addr + offset)[0]; }
    setInt32(offset, val) { new Int32Array(this.buffer, this.addr + offset)[0] = val; }
    getUint32(offset) { return new Uint32Array(this.buffer, this.addr + offset)[0]; }
    setUint32(offset, val) { new Uint32Array(this.buffer, this.addr + offset)[0] = val; }
}

export class RemoteFunction extends Function {
    constructor(libfibre, name, handle, inputArgs, outputArgs) {
        let closure = function(...args) {
            return closure._call(...args);
        }
        closure._libfibre = libfibre;
        closure._name = name;
        closure._handle = handle;
        closure._inputArgs = inputArgs;
        closure._outputArgs = outputArgs;
        // Return closure instead of the original "this" object that was
        // associated with the constructor call.
        return Object.setPrototypeOf(closure, new.target.prototype);
    }

    start() {
        const call = new Call(this._libfibre, this._outputArgs);
        this._libfibre.enqueueTask((task) => {
            task.type = FIBRE_CALL_TYPE_START_CALL;
            task.handle = call._id;
            task.startCall.func = this._handle;
            task.startCall.domain = 0; // TODO: set domain handle
        });
        return [call.txSocket, call.argCollector];
    }

    async _call(...args) {
        if (args.length != this._inputArgs.length) {
            throw Error("expected " + this._inputArgs.length + " arguments but got " + args.length);
        }

        // Preallocate array of chunks and array for the raw TX data
        const txLen = this._inputArgs.reduce((a, b) => a + b.codec.getSize(), 0);
        const txChunkBuf = this._libfibre.mallocStructArray(LibFibreChunk, 2 * args.length); // data + boundary chunk for each TX argument
        const txBufPtr = this._libfibre.wasm.malloc(txLen);

        //const rxLen = this._outputArgs.reduce((a, b) => a + b.codec.getSize(), 0);
        //const rxBufPtr = this._libfibre.wasm.malloc(rxLen);
        //const txEndPtrPtr = this._libfibre.wasm.malloc(ptrSize);
        //const rxEndPtrPtr = this._libfibre.wasm.malloc(ptrSize);
        //const handlePtr = this._libfibre.wasm.malloc(ptrSize);

        try {
            // keep track of the objects whose IDs we serialized. If any of
            // these objects disappears during the call we must cancel the call.
            const outputObjects = [];

            // Serialize output arguments
            let pos = 0;
            for (let i = 0; i < args.length; ++i) {
                const len = this._inputArgs[i].codec.serialize(this._libfibre, args[i], txBufPtr + pos);

                const c0 = txChunkBuf.add(2 * i).deref();
                c0.layer = 0;
                c0.begin = txBufPtr + pos;
                c0.end = txBufPtr + pos + len;

                const c1 = txChunkBuf.add(2 * i + 1).deref();
                c1.layer = 0;
                c1.begin = 0;
                c1.end = 0xffffffff;

                pos += len;

                if (this._inputArgs[i].codecName == 'object_ref') {
                    outputObjects.push(args[i]);
                }
            }

            var handlesStillValid = () => outputObjects.every((obj) => obj._handle);

            const [txSocket, argCollector] = this.start();
            await txSocket.writeAll(txChunkBuf, txChunkBuf.add(2 * args.length), FIBRE_STATUS_CLOSED);

            const outputs = []
            for await (let value of argCollector) {
                outputs.push(value);
            }

            if (outputs.length == 1) {
                return outputs[0];
            } else if (outputs.length > 1) {
                return outputs;
            }

        } finally {
            this._libfibre.wasm.free(txChunkBuf.addr);
            this._libfibre.wasm.free(txBufPtr);
        }
    }
}

class LibFibre {
    constructor(wasm, log_verbosity) {
        this.wasm = wasm;

        // The following functions are JavaScript callbacks that get invoked from C
        // code (WebAssembly). We convert each function to a pointer which can then
        // be passed to C code.
        // Function signature codes are documented here:
        // https://github.com/aheejin/emscripten/blob/master/site/source/docs/porting/connecting_cpp_and_javascript/Interacting-with-code.rst#calling-javascript-functions-as-function-pointers-from-c

        this._log = wasm.addFunction((ctx, file, line, level, info0, info1, text) => {
            file = this.wasm.UTF8ArrayToString(this.wasm.Module.HEAPU8, file);
            text = this.wasm.UTF8ArrayToString(this.wasm.Module.HEAPU8, text);
            if (level <= 2) {
                console.error("[" + file + ":" + line + "] " + text);
            } else if (level <= 3) {
                console.warn("[" + file + ":" + line + "] " + text);
            } else if (level <= 4) {
                console.log("[" + file + ":" + line + "] " + text);
            } else {
                console.debug("[" + file + ":" + line + "] " + text);
            }
        }, 'viiiiiii');

        this._onPost = wasm.addFunction((cb, cbCtx) => {
            const func = this.wasm.Module.asm.__indirect_function_table.get(cb);
            new Promise((resolve, reject) => {
                resolve();
            }).then(() => {
                func(cbCtx);
            });
            return 0;
        }, 'iii');

        this._onCallLater = wasm.addFunction((delay, cb, cbCtx) => {
            console.log("event loop call later");
            return setTimeout(() => cb(cbCtx), delay * 1000);
        }, 'ifii')

        this._onCancelTimer = wasm.addFunction((timer) => {
            return cancelTimeout(timer);
        }, 'ii')

        this._onStartDiscovery = wasm.addFunction((ctx, domainHandle, specs, specsLength) => {
            console.log("start discovery for domain", domainHandle);
            const discoverer = this._deref(ctx);
            discoverer.startChannelDiscovery(
                this.wasm.UTF8ArrayToString(this.wasm.Module.HEAPU8, specs, specsLength),
                domainHandle);
        }, 'viiii')

        this._onStopDiscovery = wasm.addFunction((ctx, status) => {
            console.log("discovery stopped with status ", status);
            this._freeRef(ctx);
        }, 'viiii')

        this._onFoundObject = wasm.addFunction((ctx, obj, intf, path, pathLength) => {
            const discovery = this._deref(ctx);
            const jsObj = this._loadJsObj(obj, intf)
            discovery.onFoundObject(jsObj); // TODO: load interface name from libfibre
        }, 'viiiii')

        this._onLostObject = wasm.addFunction((ctx, obj) => {
            const discovery = this._deref(ctx);
            this._releaseJsObj(obj);
        }, 'vii')

        this._onCallCompleted = wasm.addFunction((ctx, status, txEnd, rxEnd, txBufPtr, txLenPtr, rxBufPtr, rxLenPtr) => {
            let completer = this._deref(ctx);
            const [retStatus, txBuf, txLen, rxBuf, rxLen] = completer(status, txEnd, rxEnd);
            this._setIntPtr(txBufPtr, txBuf);
            this._setIntPtr(txLenPtr, txLen);
            this._setIntPtr(rxBufPtr, rxBuf);
            this._setIntPtr(rxLenPtr, rxLen);
            return retStatus;
        }, 'iiiiiiiii');

        this._runTasks = wasm.addFunction((ctx, tasks, nTasks, outTasksPtr, nOutTasksPtr) => {
            assert(!this._inDispatcher);
            this.handleTasks(tasks, nTasks);

            // Move new tasks to the shadow task queue so we can keep them valid
            // until the next invokation of _on_run_tasks.
            if (this._shadowTasks) {
                this.wasm.free(this._shadowTasks.addr);
            }
            this._shadowTasks = this._tasks;
            this._setIntPtr(outTasksPtr, this._shadowTasks.addr);
            this._setIntPtr(nOutTasksPtr, this._nPendingTasks);
            this.allocTasks();
        }, 'viiiii');

        const ptr = this.wasm.Module._libfibre_get_version();
        const version_array = (new Uint16Array(wasm.Module.HEAPU8.buffer, ptr, 3))
        this.version = {
            major: version_array[0],
            minor: version_array[1],
            patch: version_array[2]
        };
        if ((this.version.major, this.version.minor) != (0, 3)) {
            throw Error("incompatible libfibre version " + JSON.stringify(this.version));
        }


        this._intfMap = {};
        this._refMap = {};
        this._objMap = {};
        this._funcMap = {};
        this._domainMap = {};

        this.allocTasks();
        this._autoStartDispatcher = true;
        this._inDispatcher = false;

        const event_loop = wasm.malloc(4 * 5);
        try {
            const event_loop_array = (new Uint32Array(wasm.Module.HEAPU8.buffer, event_loop, 5));
            event_loop_array[0] = this._onPost;
            event_loop_array[1] = 0; // register_event
            event_loop_array[2] = 0; // deregister_event
            event_loop_array[3] = this._onCallLater;
            event_loop_array[4] = this._onCancelTimer;

            const logger = wasm.malloc(4 * 2);
            try {
                const logger_array = (new Uint32Array(wasm.Module.HEAPU8.buffer, logger, 2));
                logger_array[0] = log_verbosity;
                logger_array[1] = this._log;
                logger_array[2] = 0;

                this._handle = this.wasm.Module._libfibre_open(event_loop, this._runTasks, logger);

            } finally {
                wasm.free(logger);
            }
        } finally {
            wasm.free(event_loop);
        }
    }

    openDomain(filter) {
        const len = this.wasm.Module.lengthBytesUTF8(filter);
        const buf = this.wasm.malloc(len + 1);
        try {
            this.wasm.stringToUTF8(filter, buf, len + 1);
            var domainHandle = this.wasm.Module._libfibre_open_domain(this._handle, buf, len);
        } finally {
            this.wasm.free(buf);
        }
        console.log("opened domain", domainHandle);
        return new Domain(this, domainHandle);
    }

    /**
     * @brief Allocates a unique integer key for the specified JavaScript object.
     * 
     * The unique key can later be used to look up the JavaScript object using
     * _freeRef().
     */
    _allocRef(obj) {
        let id = 0;
        while (++id in this._refMap) {}
        //console.log("allocating id " + id, obj);
        this._refMap[id] = obj;
        return id;
    }

    _deref(id) {
        return this._refMap[id];
    }

    /**
     * @bried Deallocates a unique integer key that was previously obtained with
     * _allocRef.
     * 
     * @return The JavaScript object that was passed to the corresponding
     * _allocRef call.
     */
    _freeRef(id) {
        let obj = this._refMap[id];
        delete this._refMap[id];
        //console.log("freeing id " + id, obj);
        return obj;
    }

    _getIntPtr(ptr) {
        return new Uint32Array(this.wasm.Module.HEAPU8.buffer, ptr, 1)[0]
    }

    _setIntPtr(ptr, val) {
        new Uint32Array(this.wasm.Module.HEAPU8.buffer, ptr, 1)[0] = val;
    }

    * _getStringList(ptr) {
        for (let i = 0; this._getIntPtr(ptr + i * ptrSize); ++i) {
            yield this.wasm.UTF8ArrayToString(this.wasm.Module.HEAPU8, this._getIntPtr(ptr + i * ptrSize));
        }
    }

    _withOutputArg(func) {
        let ptrs = [];
        try {
            for (let i = 0; i < func.length; ++i) {
                ptrs.push(this.wasm.malloc(ptrSize));
            }
            func(...ptrs);
            return ptrs.map((ptr) => this._getIntPtr(ptr));
        } finally {
            for (let ptr of ptrs) {
                this.wasm.free(ptr);
            }
        }
    }

    * _decodeArgList(argNames, codecNames) {
        argNames = [...this._getStringList(argNames)];
        codecNames = [...this._getStringList(codecNames)];
        assert(argNames.length == codecNames.length);
        for (let i in argNames) {
            if (!(codecNames[i] in codecs)) {
                throw Error("unknown codec " + codecNames[i]);
            }
            yield {
                name: argNames[i],
                codecName: codecNames[i],
                codec: codecs[codecNames[i]]
            };
        }
    }

    _loadJsFunc(funcHandle) {
        let func;
        if (funcHandle in this._funcMap) {
            func = this._funcMap[funcHandle];
        } else {
            const funcInfo = this.wasm.Module._libfibre_get_function_info(funcHandle);
            try {
                const funcInfoView = (new Uint32Array(wasm.Module.HEAPU8.buffer, funcInfo, 6));
                const name = funcInfoView[0];
                const nameLength = funcInfoView[1];
                const inputNames = funcInfoView[2];
                const inputCodecs = funcInfoView[3];
                const outputNames = funcInfoView[4];
                const outputCodecs = funcInfoView[5];

                func = new RemoteFunction(this,
                    this.wasm.UTF8ArrayToString(this.wasm.Module.HEAPU8, name, nameLength),
                    funcHandle,
                    [...this._decodeArgList(inputNames, inputCodecs)],
                    [...this._decodeArgList(outputNames, outputCodecs)]);
            } finally {
                this.wasm.Module._libfibre_free_function_info(funcInfo);
            }

            this._funcMap[funcHandle] = func;
        }

        // TODO: refcount
        return func;
    }

    _loadJsIntf(intfHandle) {
        let jsIntf;
        if (intfHandle in this._intfMap) {
            jsIntf = this._intfMap[intfHandle];
        } else {
            assert(intfHandle);
            const intfInfo = this.wasm.Module._libfibre_get_interface_info(intfHandle);

            try {
                const intfInfoView = (new Uint32Array(wasm.Module.HEAPU8.buffer, intfInfo, 6));
                const name = intfInfoView[0];
                const nameLength = intfInfoView[1];
                const nAttributes = intfInfoView[3];
                const attributes = (new Uint32Array(wasm.Module.HEAPU8.buffer, intfInfoView[2], 3 * nAttributes));
                const nFunctions = intfInfoView[5];
                const functions = (new Uint32Array(wasm.Module.HEAPU8.buffer, intfInfoView[4], nFunctions));

                let jsName = this.wasm.UTF8ArrayToString(this.wasm.Module.HEAPU8, name, nameLength);
                class RemoteObject extends RemoteInterface {
                    constructor(libfibre, handle) {
                        super(libfibre);
                        this._handle = handle;
                    }
                };

                jsIntf = RemoteObject;
                jsIntf._intfName = jsName;
                jsIntf._intfHandle = intfHandle;
                
                // Attach functions to JS interface
                for (let i = 0; i < nFunctions; ++i) {
                    const func = this._loadJsFunc(functions[i]);
                    Object.defineProperty(jsIntf.prototype, func._name, {
                        get: function() {
                            const obj = this;
                            return function(...args) { return func(obj, ...args); };
                        },
                        enumerable: true
                    });
                }

                // Attach attributes to JS interface
                for (let i = 0; i < nAttributes; ++i) {
                    const name = attributes[i * 3 + 0];
                    const nameLength = attributes[i * 3 + 1];
                    const intfHandle = attributes[i * 3 + 2];
                    Object.defineProperty(jsIntf.prototype, this.wasm.UTF8ArrayToString(this.wasm.Module.HEAPU8, name, nameLength), {
                        get: function () {
                            const obj = this;
                            const [childHandle] = this._libfibre._withOutputArg((objHandlePtr) => 
                                assert(this._libfibre.wasm.Module._libfibre_get_attribute(jsIntf._intfHandle, obj._handle, i, objHandlePtr) == FIBRE_STATUS_OK));
                            if (childHandle in obj._children) {
                                return this._libfibre._objMap[childHandle];
                            } else {
                                const child = this._libfibre._loadJsObj(childHandle, intfHandle);
                                obj._children.push(childHandle);
                                return child;
                            }
                        },
                        enumerable: true
                    });
                }

            } finally {
                this.wasm.Module._libfibre_free_interface_info(intfInfo);
            }

            this._intfMap[intfHandle] = jsIntf;
        }

        return jsIntf;
    }

    _loadJsObj(objHandle, intfHandle) {
        let jsObj;
        if (objHandle in this._objMap) {
            jsObj = this._objMap[objHandle];
        } else {
            const jsIntf = this._loadJsIntf(intfHandle);
            jsObj = new jsIntf(this, objHandle);
            this._objMap[objHandle] = jsObj;
        }

        // Note: this refcount does not count the JS references to the object
        // but rather mirrors the libfibre-internal refcount of the object. This
        // is so that we can destroy the JS object when libfibre releases it.
        jsObj._refCount++;
        return jsObj;
    }

    _releaseJsObj(objHandle) {
        const jsObj = this._objMap[objHandle];
        assert(jsObj);
        jsObj._refCount--;
        if (jsObj._refCount <= 0) {
            const children = jsObj._children;
            delete this._objMap[objHandle];
            delete jsObj._handle;
            delete jsObj._children;
            Object.setPrototypeOf(jsObj, _staleObject);

            for (const child of children) {
                this._releaseJsObj(child);
            }

            jsObj._oldHandle = objHandle;
            jsObj._onLostResolve();
        }
    }

    enqueueTask(taskFactory) {
        // Enlarge array by factor 5 if necessary
        if (this._nPendingTasks >= this._nAllocatedTasks) {
            const newTasks = this.mallocStructArray(5 * this._nAllocatedTasks);
            this.wasm.Module.HEAPU8.set(this.wasm.Module.HEAPU8.subarray(this._tasks, this._nAllocatedTasks * LibFibreTask.size), newTasks);
            this._tasks = newTasks;
            this._nAllocatedTasks *= 5;
        }

        taskFactory(this._tasks.add(this._nPendingTasks).deref());
        this._nPendingTasks++;

        // Dispatch all tasks at the next opportunity
        if (this._autoStartDispatcher) {
            this._autoStartDispatcher = false;
            new Promise((resolve) => resolve()).then(() => this.dispatchTasksToLib());
        }
    }

    freeTasks() {
        this.wasm.free(this._tasks.addr);
    }

    allocTasks() {
        // Pre-allocate some arbitrary-sized task array
        this._nAllocatedTasks = 10;
        this._nPendingTasks = 0;
        this._tasks = this.mallocStructArray(LibFibreTask, this._nAllocatedTasks);
    }

    dispatchTasksToLib() {
        this._inDispatcher = true;

        while (this._nPendingTasks) {
            const [outTasks, nOutTasks] = this._withOutputArg((outTasksPtr, nOutTasksPtr) =>
                this.wasm.Module._libfibre_run_tasks(this._handle, this._tasks.addr, this._nPendingTasks, outTasksPtr, nOutTasksPtr));
            this.freeTasks();
            this.allocTasks();
            this.handleTasks(outTasks, nOutTasks);
        }

        this._autoStartDispatcher = true;
        this._inDispatcher = false;
    }

    handleTasks(tasks, nTasks) {
        tasks = new Pointer(this.wasm.Module.HEAPU8.buffer, LibFibreTask, tasks);
        for (var i = 0; i < nTasks; ++i) {
            const task = tasks.add(i).deref(i);
            if (task.type == FIBRE_CALL_TYPE_START_CALL) {
                throw "function server not implemented";
            } else if (task.type == FIBRE_CALL_TYPE_WRITE) {
                this._deref(task.handle).argCollector._onWrite(task.write);
            } else if (task.type == FIBRE_CALL_TYPE_WRITE_DONE) {
                this._deref(task.handle).txSocket._onWriteDone(task.writeDone);
            } else {
                throw "unknown task type: " + task.type;
            }
        }
    }

    mallocStructArray(structType, nElements) {
        const ptr = this.wasm.malloc(structType.size * nElements);
        return new Pointer(this.wasm.Module.HEAPU8.buffer, structType, ptr);
    }
}

class Discovery {
    
};

class Domain {
    constructor(libfibre, handle) {
        this._libfibre = libfibre;
        this._handle = handle;
        this._libfibre._domainMap[handle] = this;
    }

    showDeviceDialog(backend) {
        const len = this._libfibre.wasm.Module.lengthBytesUTF8(backend);
        const buf = this._libfibre.wasm.malloc(len + 1);
        try {
            this._libfibre.wasm.stringToUTF8(backend, buf, len + 1);
            this._libfibre.wasm.Module._libfibre_show_device_dialog(this._handle, buf);
        } finally {
            this._libfibre.wasm.free(buf);
        }
    }

    startDiscovery(onFoundObject) {
        let discovery = new Discovery();
        discovery.onFoundObject = onFoundObject;

        return this._libfibre._withOutputArg((discoveryHandlePtr) => 
            this._libfibre.wasm.Module._libfibre_start_discovery(this._handle,
                discoveryHandlePtr, this._libfibre._onFoundObject,
                this._libfibre._onLostObject,
                this._libfibre._onStopped,
                this._libfibre._allocRef(discovery)));
    }

    stopDiscovery(discovery) {
        this._libfibre.wasm.Module._libfibre_stop_discovery(this._handle);
    }
}

export function fibreOpen(log_verbosity = 5) {
    return new Promise(async (resolve) => {
        let Module = {
            instantiateWasm: async (info, receiveInstance) => {
                const isWebpack = typeof __webpack_require__ === 'function';
                const wasmPath = isWebpack ? (await import("!!file-loader!./libfibre-wasm.wasm")).default
                    : "./libfibre-wasm.wasm";

                let result;
                if (typeof navigator === 'object') {
                    // Running in browser
                    const response = fetch(wasmPath, { credentials: 'same-origin' });
                    result = await WebAssembly.instantiateStreaming(response, info);
                } else {
                    // Running in bare NodeJS
                    const fsPromises = (await import('fs/promises')).default;
                    const response = await fsPromises.readFile('/Data/Projects/fibre/js/libfibre-wasm.wasm');
                    result = await WebAssembly.instantiate(response, info);
                }
                receiveInstance(result['instance']);
                return {};
            }
        };
        Module = await wasm(Module);
        await Module.ready;
        wasm.addFunction = Module.addFunction;
        wasm.stringToUTF8 = Module.stringToUTF8;
        wasm.UTF8ArrayToString = Module.UTF8ArrayToString;
        wasm.malloc = Module._malloc;
        wasm.free = Module._free;
        wasm.Module = Module;
        
        resolve(new LibFibre(wasm, log_verbosity));
    });
}
