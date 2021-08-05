
mergeInto(LibraryManager.library, {
  $method_support__postset: 'method_support();',
  $method_support: function() {
    let _root = Function('return this')();
    let _objects = {0: {o: _root, n: 1}}; // key: id, value: object

    const kTypeUndefined = 0;
    const kTypeInt = 1;
    const kTypeString = 2;
    const kTypeList = 3;
    const kTypeDict = 4;
    const kTypeObject = 5;
    const kTypeFunc = 6;
    const kTypeArray = 7;

    function exportObject(obj) {
      for (const id in _objects) {
        if (_objects[id].o == obj) {
          _objects[id].n++;
          return id;
        }
      }

      let id = 0;
      while (++id in _objects) {}
      _objects[id] = {o: obj, n: 1};
      return id;
    }

    function releaseObject(id) {
      if (--_objects[id].n == 0) {
        delete _objects[id];
      }
    }

    function makeStorage() {
      return {stubs: [], objects: [], strings: [], arrays: []};
    }

    function deleteStorage(storage) {
      for (let i in storage.stubs) {
        Module._free(storage.stubs[i]);
      }
      for (let i in storage.strings) {
        Module._free(storage.strings[i]);
      }
      for (let i in storage.arrays) {
        Module._free(Module.HEAPU32[storage.arrays[i] >> 2]);
        Module._free(storage.arrays[i]);
      }
      for (let i in storage.objects) {
        releaseObject(storage.objects[i]);
      }
    }

    function fromWasm(jsStubPtr) {
      const type = Module.HEAPU32[jsStubPtr >> 2];
      const val = Module.HEAPU32[(jsStubPtr >> 2) + 1];

      if (type == kTypeInt) {
        return val;
      } else if (type == kTypeString) {
        return Module.UTF8ArrayToString(Module.HEAPU8, val);
      } else if (type == kTypeList) {
        const len = fromWasm(val);
        return [...Array(len).keys()].map((i) => fromWasm(val + 8 + 8 * i));
      } else if (type == kTypeDict) {
        const len = fromWasm(val);
        return Object.assign({}, ...[...Array(len).keys()].map((i) => ({
          [fromWasm(val + 8 + 16 * i)]: fromWasm(val + 16 + 16 * i)
        })));
      } else if (type == kTypeFunc) {
        const callback = Module.HEAPU32[(val >> 2)];
        const ctx = Module.HEAPU32[(val >> 2) + 1];
        return () => {
          callWasm2(callback, ctx, arguments);
        };
      } else if (type == kTypeArray) {
        const start = Module.HEAPU32[(val >> 2)];
        const end = Module.HEAPU32[(val >> 2) + 1];
        const theArr = Module.HEAPU8.slice(start, end);
        return theArr; // This does not copy the array contents
      } else {
        throw "stub type not supported: " + type;
      }
    }

    function toWasm(storage, val, jsStubPtr, dictDepth = 0) {
      if ((typeof val) == "undefined") {
        Module.HEAPU32[jsStubPtr >> 2] = kTypeUndefined;
        Module.HEAPU32[(jsStubPtr >> 2) + 1] = 0;

      } else if ((typeof val) == "number") {
        Module.HEAPU32[jsStubPtr >> 2] = kTypeInt;
        Module.HEAPU32[(jsStubPtr >> 2) + 1] = val;

      } else if ((typeof val) == "string") {
        const strLen = Module.lengthBytesUTF8(val) + 1
        const strPtr = Module._malloc(strLen);
        storage.strings.push(strPtr);
        Module.stringToUTF8(val, strPtr, strLen);
        Module.HEAPU32[jsStubPtr >> 2] = kTypeString;
        Module.HEAPU32[(jsStubPtr >> 2) + 1] = strPtr;
        
      } else if ((typeof val) == "object" && Array.isArray(val)) {
        const ptr = Module._malloc(8 * (val.length + 1));
        storage.stubs.push(ptr);
        toWasm(storage, val.length, ptr);
        for (let i = 0; i < val.length; ++i) {
          toWasm(storage, val[i], ptr + 8 * (i + 1), dictDepth - 1);
        }
        Module.HEAPU32[jsStubPtr >> 2] = kTypeList;
        Module.HEAPU32[(jsStubPtr >> 2) + 1] = ptr;

      } else if ((typeof val) == "object" && dictDepth > 0) {
        let len = 0;
        for(var key in val) len++;
        const ptr = Module._malloc(8 * (2 * len + 1));
        storage.stubs.push(ptr);
        toWasm(storage, len, ptr);
        let i = 0;
        for (let k in val) {
          toWasm(storage, k, ptr + 8 * (2 * i + 1), dictDepth - 1);
          toWasm(storage, val[k], ptr + 8 * (2 * i + 2), dictDepth - 1);
          ++i;
        }
        Module.HEAPU32[jsStubPtr >> 2] = kTypeDict;
        Module.HEAPU32[(jsStubPtr >> 2) + 1] = ptr;
      
      
      } else if (ArrayBuffer.isView(val)) {
        const ptr = Module._malloc(val.byteLength);
        Module.HEAPU8.set(new Uint8Array(val.buffer, val.byteOffset, val.byteLength), ptr);
        const arrPtr = Module._malloc(8);
        Module.HEAPU32[arrPtr >> 2] = ptr;
        Module.HEAPU32[(arrPtr >> 2) + 1] = ptr + val.byteLength;
        storage.arrays.push(arrPtr);
        Module.HEAPU32[jsStubPtr >> 2] = kTypeArray;
        Module.HEAPU32[(jsStubPtr >> 2) + 1] = arrPtr;

      } else if ((typeof val) == "object") {
        const objId = exportObject(val);
        storage.objects.push(objId);
        Module.HEAPU32[jsStubPtr >> 2] = kTypeObject;
        Module.HEAPU32[(jsStubPtr >> 2) + 1] = objId;

      } else {
        throw "type " + (typeof val) + " cannot be passed to WASM";
      }
    }

    function callWasm(callback, ctx, arg, dictDepth) {
      const cb = Module.asm.__indirect_function_table.get(callback);
      const storage = makeStorage();
      try {
        const ptr = Module._malloc(8);
        try {
          toWasm(storage, arg, ptr, dictDepth);
          cb(ctx, ptr);
        } finally {
          Module._free(ptr);
        }
      } finally {
        deleteStorage(storage);
      }
    }

    function callWasm2(callback, ctx, args) {
      const cb = Module.asm.__indirect_function_table.get(callback);
      const storage = makeStorage();
      try {
        const ptr = Module._malloc(8 + args.length);
        try {
          for (let i = 0; i < args.length; ++i) {
            toWasm(storage, args[i], ptr + 8 * i);
          }
          cb(ctx, ptr, args.length);
        } finally {
          Module._free(ptr);
        }
      } finally {
        deleteStorage(storage);
      }
    }

    const _js_ref = function(obj) {
      _objects[obj].n++;
    }

    const _js_unref = function(obj) {
      releaseObject(obj);
    }

    const _js_call_async = function(obj, func, args, nArgs, callback, ctx, dictDepth) {
      const funcName = Module.UTF8ArrayToString(Module.HEAPU8, func);
      const argList = [...Array(nArgs).keys()].map((i) => fromWasm(args + 8 * i));
      _objects[obj].o[funcName](...argList).then((result) => {
        callWasm(callback, ctx, result, dictDepth)
      });
    }

    const _js_get_property = function(obj, property, callback, ctx, dictDepth) {
      const propName = Module.UTF8ArrayToString(Module.HEAPU8, property);
      //console.log("get property ", propName, " of ", obj, _objects[obj]);
      const result = _objects[obj].o[propName];
      callWasm(callback, ctx, result, dictDepth);
    }

    const _js_set_property = function(obj, property, arg) {
      const propName = Module.UTF8ArrayToString(Module.HEAPU8, property);
      //console.log("set property ", propName, " of ", _objects[obj].o, " to ", fromWasm(arg));
      _objects[obj].o[propName] = fromWasm(arg);
    }

    __js_ref = _js_ref;
    __js_unref = _js_unref;
    __js_call_async = _js_call_async;
    __js_get_property = _js_get_property;
    __js_set_property = _js_set_property;
  },

  _js_ref: function() {},
  _js_ref__deps: ['$method_support'],
  _js_unref: function() {},
  _js_unref__deps: ['$method_support'],
  _js_call_async: function() {},
  _js_call_async__deps: ['$method_support'],
  _js_get_property: function() {},
  _js_get_property__deps: ['$method_support'],
  _js_set_property: function() {},
  _js_set_property__deps: ['$method_support'],
});
