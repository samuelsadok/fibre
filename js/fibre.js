
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

const ptrSize = 4;

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
        this.deserialize = (libfibre, ptr) => {
            return new DataView(libfibre.wasm.Module.HEAPU8.buffer, ptr)['get' + typeName](0, littleEndian);
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
        this.deserialize = (libfibre, ptr) => {
            return !!(new DataView(libfibre.wasm.Module.HEAPU8.buffer, ptr).getUint8(0));
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
        this.deserialize = (libfibre, ptr) => {
            const handle = new DataView(libfibre.wasm.Module.HEAPU8.buffer, ptr).getUint32(0, true);
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

class TxStream {
    constructor(libfibre, handle) {
        this._libfibre = libfibre;
        this._handle = handle;
        this.isClosed = false;
        this._libfibre._txStreamMap[this._handle] = this;
    }

    write(data) {
        assert(this._handle);
        return new Promise((resolve, reject) => {
            let byteLength = Array.isArray(data) ? data.length : data.byteLength;
            if (!Array.isArray(data)) {
                data = new Uint8Array(data.buffer, data.byteOffset, data.byteLength)
            }
            this._buf = [this._libfibre.wasm.malloc(byteLength), byteLength];
            this._libfibre.wasm.Module.HEAPU8.set(data, this._buf[0]);
            this._writeResolve = resolve;
            this._writeReject = reject;
            this._libfibre.wasm.Module._libfibre_start_tx(this._handle, this._buf[0], this._buf[1], this._libfibre._onTxCompleted, this._handle);
        });
    }

    async writeAll(data) {
        while (true) {
            const nWritten = await this.write(data);
            data = data.slice(nWritten);
            if (data.length == 0) {
                break;
            }
            if (this.isClosed) {
                throw EofError(`the TX stream was closed but there are still ${data.length} bytes left to send`);
            }
            assert(nWritten > 0); // ensure progress
        }
    }

    close(status) {
        assert(this._handle);
        this._libfibre.wasm.Module._libfibre_close_tx(this._handle, status);
        delete this._handle;
    }

    _complete(status, txEnd) {
        const len = txEnd - this._buf[0];
        assert(len <= this._buf[1]);
        this._libfibre.wasm.free(this._buf[0]);
        delete this._buf;
        const resolve = this._writeResolve;
        const reject = this._writeReject;
        delete this._writeResolve;
        delete this._writeReject;

        this.isClosed = this.isClosed || (status == FIBRE_STATUS_CLOSED);

        if (status == FIBRE_STATUS_OK || status == FIBRE_STATUS_CLOSED) {
            resolve(len);
        } else {
            reject(_getError(status));
        }
    }
}

class RxStream {
    constructor(libfibre, handle) {
        this._libfibre = libfibre;
        this._handle = handle;
        this.isClosed = false;
        this._libfibre._rxStreamMap[this._handle] = this;
    }

    read(len) {
        assert(this._handle);
        return new Promise((resolve, reject) => {
            this._buf = [this._libfibre.wasm.malloc(len), len];
            this._readResolve = resolve;
            this._readReject = reject;
            this._libfibre.wasm.Module._libfibre_start_rx(this._handle, this._buf[0], this._buf[1], this._libfibre._onRxCompleted, this._handle);
        });
    }

    async readAll(len) {
        let data = [];
        while (true) {
            let chunk = await this.read(len - data.length);
            data.push(...chunk);
            if (data.length >= len) {
                break;
            }
            if (this.isClosed) {
                throw EofError(`the RX stream was closed but there are still ${len - data.length} bytes left to receive`);
            }
            assert(chunk.length > 0); // ensure progress
        }
        return data;
    }

    close(status) {
        assert(this._handle);
        this._libfibre.wasm.Module._libfibre_close_rx(this._handle, status);
        delete this._handle;
    }

    _complete(status, rxEnd) {
        const len = rxEnd - this._buf[0];
        assert(len <= this._buf[1]);
        const result = new Uint8Array(this._libfibre.wasm.Module.HEAPU8.buffer, this._buf[0], len).slice(0);
        this._libfibre.wasm.free(this._buf[0]);
        delete this._buf;
        const resolve = this._readResolve;
        const reject = this._readReject;
        delete this._readResolve;
        delete this._readReject;

        this.isClosed = this.isClosed || (status == FIBRE_STATUS_CLOSED);

        if (status == FIBRE_STATUS_OK || status == FIBRE_STATUS_CLOSED) {
            resolve(result);
        } else {
            reject(_getError(status));
        }
    }
}

export class RemoteInterface {
    constructor(libfibre, intfName) {
        this._libfibre = libfibre;
        this._refCount = 0;
        this._children = [];
        this._intfName = intfName;
        this._onLost = new Promise((resolve) => this._onLostResolve = resolve);
    }
}

const _staleObject = {};

class RemoteAttribute {
    constructor(libfibre, handle, subintf, subintfName) {
        this._libfibre = libfibre;
        this._handle = handle;
        this._subintf = subintf;
        this._subintfName = subintfName;
    }

    get(obj) {
        const objHandle = this._libfibre._withOutputArg((objHandlePtr) => 
            this._libfibre.wasm.Module._libfibre_get_attribute(obj._handle, this._handle, objHandlePtr));
        if (objHandle in obj._children) {
            return this._libfibre._objMap[objHandle];
        } else {
            const child = this._libfibre._loadJsObj(objHandle, this._subintf, this._subintfName);
            obj._children.push(objHandle);
            return child;
        }
    }
}

export class RemoteFunction extends Function {
    constructor(libfibre, handle, inputArgs, outputArgs) {
        let closure = function(...args) { 
            return closure._call(this, ...args);
        }
        closure._libfibre = libfibre;
        closure._handle = handle;
        closure._inputArgs = inputArgs;
        closure._outputArgs = outputArgs;
        // Return closure instead of the original "this" object that was
        // associated with the constructor call.
        return Object.setPrototypeOf(closure, new.target.prototype);
    }

    async _call(...args) {
        if (args.length != this._inputArgs.length) {
            throw Error("expected " + this._inputArgs.length + " arguments but got " + args.length);
        }

        const txLen = this._inputArgs.reduce((a, b) => a + b.codec.getSize(), 0);
        const rxLen = this._outputArgs.reduce((a, b) => a + b.codec.getSize(), 0);
        const txBufPtr = this._libfibre.wasm.malloc(txLen);
        const rxBufPtr = this._libfibre.wasm.malloc(rxLen);
        const txEndPtrPtr = this._libfibre.wasm.malloc(ptrSize);
        const rxEndPtrPtr = this._libfibre.wasm.malloc(ptrSize);
        const handlePtr = this._libfibre.wasm.malloc(ptrSize);

        try {
            // keep track of the objects whose IDs we serialized. If any of
            // these objects disappears during the call we must cancel the call.
            const outputObjects = [];

            // Serialize output arguments
            let pos = 0;
            for (let i = 0; i < args.length; ++i) {
                pos += this._inputArgs[i].codec.serialize(this._libfibre, args[i], txBufPtr + pos);
                if (this._inputArgs[i].codecName == 'object_ref') {
                    outputObjects.push(args[i]);
                }
            }

            var handlesStillValid = () => outputObjects.every((obj) => obj._handle);
            
            let txPos = 0;
            let rxPos = 0;
            this._libfibre._setIntPtr(handlePtr, 0);

            do {
                let callCallback;
                const promise = new Promise((resolve, reject) => 
                    callCallback = (status, txEnd, rxEnd) => {
                        txPos = txEnd - txBufPtr;
                        rxPos = rxEnd - rxBufPtr;

                        if (status == FIBRE_STATUS_OK) {
                            assert(rxPos < rxLen || txPos < txLen);
                            assert(handlesStillValid());
                            return [
                                FIBRE_STATUS_CLOSED,
                                txBufPtr + txPos, txLen - txPos,
                                rxBufPtr + rxPos, rxLen - rxPos,
                            ];
                        } else {
                            resolve(status);
                            return [FIBRE_STATUS_CLOSED, 0, 0, 0, 0];
                        }
                    }
                );
                let callCallbackRef = this._libfibre._allocRef(callCallback);

                try {
                    assert(handlesStillValid());
                    assert(this._handle);

                    status = this._libfibre.wasm.Module._libfibre_call(this._handle, handlePtr,
                        // We always pass the complete buffers, so if libfibre
                        // manages to process the buffers to the end the call is
                        // completed.
                        FIBRE_STATUS_CLOSED,
                        txBufPtr + txPos, txLen - txPos,
                        rxBufPtr + rxPos, rxLen - rxPos,
                        txEndPtrPtr, rxEndPtrPtr,
                        this._libfibre._onCallCompleted,
                        callCallbackRef);

                    txPos = this._libfibre._getIntPtr(txEndPtrPtr) - txBufPtr;
                    rxPos = this._libfibre._getIntPtr(rxEndPtrPtr) - rxBufPtr;

                    if (status == FIBRE_STATUS_BUSY) {
                        status = await promise;
                        assert(status != FIBRE_STATUS_BUSY);
                    }
                } finally {
                    this._libfibre._freeRef(callCallbackRef);
                }

            } while ((txPos < txLen || rxPos < rxLen) && status == FIBRE_STATUS_OK);

            if (txPos == txLen && rxPos == rxLen && status == FIBRE_STATUS_OK) {
                throw Error("libfibre ignored our close request");
            } else if (txPos < txLen && rxPos < rxLen && status == FIBRE_STATUS_CLOSED) {
                throw Error("call closed unexpectedly by remote");
            } else if (status != FIBRE_STATUS_CLOSED) {
                throw _getError(status);
            }

            let outputs = [];
            pos = 0;
            for (let argSpec of this._outputArgs) {
                outputs.push(argSpec.codec.deserialize(this._libfibre, rxBufPtr + pos))
                pos += argSpec.codec.getSize();
            }

            if (outputs.length == 1) {
                return outputs[0];
            } else if (outputs.length > 1) {
                return outputs;
            }

        } finally {
            this._libfibre.wasm.free(handlePtr);
            this._libfibre.wasm.free(rxEndPtrPtr);
            this._libfibre.wasm.free(txEndPtrPtr);
            this._libfibre.wasm.free(rxBufPtr);
            this._libfibre.wasm.free(txBufPtr);
        }
    }
}

class LibFibre {
    constructor(wasm) {
        this.wasm = wasm;

        // The following functions are JavaScript callbacks that get invoked from C
        // code (WebAssembly). We convert each function to a pointer which can then
        // be passed to C code.
        // Function signature codes are documented here:
        // https://github.com/aheejin/emscripten/blob/master/site/source/docs/porting/connecting_cpp_and_javascript/Interacting-with-code.rst#calling-javascript-functions-as-function-pointers-from-c

        this._onPost = wasm.addFunction((cb, cbCtx) => {
            // TODO
            console.log("event loop post");
            return new Promise((resolve, reject) => {
                cb(cbCtx);
                resolve();
            });
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

        this._onFoundObject = wasm.addFunction((ctx, obj, intf) => {
            const discovery = this._deref(ctx);
            discovery.onFoundObject(this._loadJsObj(obj, intf, 'anonymous_root_interface')); // TODO: load interface name from libfibre
        }, 'viii')

        this._onLostObject = wasm.addFunction((ctx, obj) => {
            const discovery = this._deref(ctx);
            this._releaseJsObj(obj);
        }, 'vii')

        this._onAttributeAdded = wasm.addFunction((ctx, attr, name, nameNength, subintf, subintfName, subintfNameLength) => {
            let jsIntf = this._intfMap[ctx];
            assert(jsIntf);
            let jsAttr = new RemoteAttribute(this, attr, subintf, this.wasm.UTF8ArrayToString(this.wasm.Module.HEAPU8, subintfName, subintfNameLength));
            Object.defineProperty(jsIntf.prototype, this.wasm.UTF8ArrayToString(this.wasm.Module.HEAPU8, name, nameNength), {
                get: function () { return jsAttr.get(this); },
                enumerable: true
            });
        }, 'viiiiiii')

        this._onAttributeRemoved = wasm.addFunction((ctx, attr) => {
            console.log("attribute removed"); // TODO
        }, 'vii')

        this._onFunctionAdded = wasm.addFunction((ctx, func, name, nameLength, inputNames, inputCodecs, outputNames, outputCodecs) => {
            const jsIntf = this._intfMap[ctx];
            let jsInputParams;
            let jsOutputParams;
            try {
                jsInputParams = [...this._decodeArgList(inputNames, inputCodecs)];
                jsOutputParams = [...this._decodeArgList(outputNames, outputCodecs)];
            } catch (err) {
                console.warn(err.message);
                return;
            }
            let jsFunc = new RemoteFunction(this, func, jsInputParams, jsOutputParams);
            Object.defineProperty(jsIntf.prototype, this.wasm.UTF8ArrayToString(this.wasm.Module.HEAPU8, name, nameLength), {
                value: jsFunc,
                enumerable: true
            });
        }, 'viiiiiiii')

        this._onFunctionRemoved = wasm.addFunction((ctx, func) => {
            console.log("function removed"); // TODO
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

        this._onTxCompleted = wasm.addFunction((ctx, txStream, status, txEnd) => {
            this._txStreamMap[txStream]._complete(status, txEnd);
        }, 'viiii')

        this._onRxCompleted = wasm.addFunction((ctx, rxStream, status, rxEnd) => {
            this._rxStreamMap[rxStream]._complete(status, rxEnd);
        }, 'viiii')

        const ptr = this.wasm.Module._libfibre_get_version();
        const version_array = (new Uint16Array(wasm.Module.HEAPU8.buffer, ptr, 3))
        this.version = {
            major: version_array[0],
            minor: version_array[1],
            patch: version_array[2]
        };
        if ((this.version.major, this.version.minor) != (0, 1)) {
            throw Error("incompatible libfibre version " + JSON.stringify(this.version));
        }


        this._intfMap = {};
        this._refMap = {};
        this._objMap = {};
        this._domainMap = {};
        this._txStreamMap = {};
        this._rxStreamMap = {};

        const event_loop = wasm.malloc(4 * 5);
        try {
            const event_loop_array = (new Uint32Array(wasm.Module.HEAPU8.buffer, event_loop, 5));
            event_loop_array[0] = this._onPost;
            event_loop_array[1] = 0; // register_event
            event_loop_array[2] = 0; // deregister_event
            event_loop_array[3] = this._onCallLater;
            event_loop_array[4] = this._onCancelTimer;
        } finally {
            wasm.free(event_loop);
        }

        this._handle = this.wasm.Module._libfibre_open(event_loop);

        this.usbDiscoverer = new WebUsbDiscoverer(this);

        let buf = [this.wasm.malloc(4), 4];
        try {
            let len = this.wasm.stringToUTF8Array("usb", this.wasm.Module.HEAPU8, buf[0], buf[1]);
            this.wasm.Module._libfibre_register_backend(this._handle, buf[0], len,
                this._onStartDiscovery, this._onStopDiscovery, this._allocRef(this.usbDiscoverer));
        } finally {
            this.wasm.free(buf[0]);
        }
    }

    addChannels(domainHandle, mtu) {
        console.log("add channel with mtu " + mtu);
        const [txChannelId, rxChannelId] = this._withOutputArg((txChannelIdPtr, rxChannelIdPtr) =>
            this.wasm.Module._libfibre_add_channels(domainHandle, txChannelIdPtr, rxChannelIdPtr, mtu)
        );
        return [
            new RxStream(this, txChannelId), // libfibre => backend
            new TxStream(this, rxChannelId) // backend => libfibre
        ];
    }

    openDomain(filter) {
        let buf = [this.wasm.malloc(filter.length + 1), filter.length + 1];
        try {
            let len = this.wasm.stringToUTF8Array(filter, this.wasm.Module.HEAPU8, buf[0], buf[1]);
            var domainHandle = this.wasm.Module._libfibre_open_domain(this._handle, buf[0], len);
        } finally {
            this.wasm.free(buf[0]);
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

    _loadJsIntf(intfHandle, intfName) {
        if (intfHandle in this._intfMap) {
            return this._intfMap[intfHandle];
        } else {
            class RemoteObject extends RemoteInterface {
                constructor(libfibre, handle) {
                    super(libfibre, intfName);
                    this._handle = handle;
                }
            };
            let jsIntf = RemoteObject;
            this._intfMap[intfHandle] = jsIntf;
            this.wasm.Module._libfibre_subscribe_to_interface(intfHandle,
                this._onAttributeAdded,
                this._onAttributeRemoved,
                this._onFunctionAdded,
                this._onFunctionRemoved,
                intfHandle);
            return jsIntf;
        }
    }

    _loadJsObj(objHandle, intfHandle, intfName) {
        let jsObj;
        if (objHandle in this._objMap) {
            jsObj = this._objMap[objHandle];
        } else {
            const jsIntf = this._loadJsIntf(intfHandle, intfName);
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
}

class Discovery {
    
};

class Domain {
    constructor(libfibre, handle) {
        this._libfibre = libfibre;
        this._handle = handle;
        this._libfibre._domainMap[handle] = this;
    }

    addChannels(mtu) {
        return this._libfibre.addChannels(this._handle, mtu);
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

export function fibreOpen() {
    return new Promise(async (resolve) => {
        let Module = {
            preRun: [function() {Module.ENV.FIBRE_LOG = "4"}], // enable logging
            instantiateWasm: async (info, receiveInstance) => {
                const isWebpack = typeof __webpack_require__ === 'function';
                const wasmPath = isWebpack ? (await import("!!file-loader!./libfibre-wasm.wasm")).default
                    : "./libfibre-wasm.wasm";
                const response = fetch(wasmPath, { credentials: 'same-origin' });
                const result = await WebAssembly.instantiateStreaming(response, info);
                receiveInstance(result['instance']);
                return {};
            }
        };
        
        Module = await wasm(Module);
        await Module.ready;
        wasm.addFunction = Module.addFunction;
        wasm.stringToUTF8Array = Module.stringToUTF8Array;
        wasm.UTF8ArrayToString = Module.UTF8ArrayToString;
        wasm.malloc = Module._malloc;
        wasm.free = Module._free;
        wasm.Module = Module;
        
        resolve(new LibFibre(wasm));
    });
}

class WebUsbDiscoverer {
    constructor(libfibre) {
        this._libfibre = libfibre;
        this._domains = [];

        if (navigator.usb.onconnect != null) {
            console.warn("There was already a subscriber for usb.onconnect.");
        }
        navigator.usb.onconnect = (event) => {
            for (let domain of this._domains) {
                this._consider(event.device, domain.filter, domain.handle);
            }
        }
        navigator.usb.ondisconnect = () => console.log("USB device disconnected");

        this._filterKeys = {
            'idVendor': 'vendorId',
            'idProduct': 'productId',
            'bInterfaceClass': 'classCode',
            'bInterfaceSubClass': 'subclassCode',
            'bInterfaceProtocol': 'protocolCode',
        };

        this.showDialog = async () => {
            let filters = this._domains.map((d) => d.filter);
            let dev = await navigator.usb.requestDevice({filters: filters});
            for (let domain of this._domains) {
                this._consider(dev, domain.filter, domain.handle);
            }
        };
    }

    async startChannelDiscovery(specs, domainHandle) {
        let filter = {}
        for (let item of specs.split(',')) {
            filter[this._filterKeys[item.split('=')[0]]] = item.split('=')[1]
        }

        this._domains.push({
            filter: filter,
            handle: domainHandle
        });

        for (let dev of await navigator.usb.getDevices({filters: [filter]})) {
            this._consider(dev, filter, domainHandle);
        }
    }

    async _consider(device, filter, domainHandle) {
        if ((filter.vendorId != undefined) && (filter.vendorId != device.vendorId)) {
            return;
        }
        if ((filter.productId != undefined) && (filter.productId != device.productId)) {
            return;
        }

        for (let config of device.configurations) {
            if (device.configuration !== null && device.configuration.configurationValue != config.configurationValue) {
                continue; // A configuration was already set and it's different from this one
            }

            for (let intf of config.interfaces) {
                for (let alternate of intf.alternates) {
                    const mismatch = ((filter.classCode != undefined) && (filter.classCode != alternate.interfaceClass))
                                  || ((filter.subclassCode != undefined) && (filter.subclassCode != alternate.interfaceSubclass))
                                  || ((filter.protocolCode != undefined) && (filter.protocolCode != alternate.interfaceProtocol));
                    if (mismatch) {
                        continue;
                    }

                    await device.open();
                    await device.selectConfiguration(config.configurationValue);
                    await device.claimInterface(intf.interfaceNumber);

                    let epOut = null, epIn = null;
                    for (let ep of alternate.endpoints) {
                        if (ep.type == "bulk" && ep.direction == "in")
                            epIn = ep;
                        else if (ep.type == "bulk" && ep.direction == "out")
                            epOut = ep;
                    }

                    device.knownInEndpoints = device.knownInEndpoints || [];
                    device.knownOutEndpoints = device.knownOutEndpoints || [];
                    console.log(device.knownOutEndpoints);
                    if (device.knownInEndpoints.indexOf(epIn.endpointNumber) >= 0) {
                        continue;
                    }
                    if (device.knownOutEndpoints.indexOf(epOut.endpointNumber) >= 0) {
                        continue;
                    }
                    device.knownInEndpoints.push(epIn.endpointNumber);
                    device.knownOutEndpoints.push(epOut.endpointNumber);

                    let mtu = Math.min(epIn.packetSize, epOut.packetSize);
                    const [txChannel, rxChannel] = this._libfibre.addChannels(domainHandle, mtu);
                    this._connectBulkInEp(device, epIn, rxChannel);
                    this._connectBulkOutEp(device, epOut, txChannel);
                }
            }
        }
    }

    async _connectBulkOutEp(dev, ep, stream) {
        while (true) {
            //console.log("waiting for data from libfibre...");
            const data = await stream.read(ep.packetSize);
            //console.log("forwarding " + data.length + " bytes to USB...");
            //console.log(data);
            let result;
            try {
                result = await dev.transferOut(ep.endpointNumber, data);
            } catch (e) { // TODO: propagate actual transfer errors
                result = {status: "stall"};
            }
            assert(result.bytesWritten == data.length);
            //console.log(dev.opened);
            if (!dev.opened || result.status == "stall") {
                stream.close(FIBRE_STATUS_CLOSED);
                break;
            }
            assert(result.status == "ok")
        };
    }

    async _connectBulkInEp(dev, ep, stream) {
        while (true) {
            //console.log("waiting for up to " + ep.packetSize + " bytes from USB...");
            let result;
            try {
                result = await dev.transferIn(ep.endpointNumber, ep.packetSize);
            } catch (e) { // TODO: propagate actual transfer errors
                result = {status: "stall"};
            }
            //console.log("got for data from USB...");
            //console.log(dev.opened);
            //console.log(dev);
            if (!dev.opened || result.status == "stall") {
                stream.close(FIBRE_STATUS_CLOSED);
                break;
            }
            assert(result.status == "ok");
            //console.log("forwarding " + result.data.byteLength + " bytes to libfibre...");
            const len = await stream.write(result.data);
            assert(len == result.data.byteLength);
        }
    }
}
