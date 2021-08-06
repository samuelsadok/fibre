
mergeInto(LibraryManager.library, {
  $method_support__postset: 'method_support();',
  $method_support: function() {

    function HandleMap() {
      this.content = {};
      this.nextId = 0;
      this.add = function(val) {
        while (this.nextId in this.content) {
          this.nextId = (this.nextId + 1) & 0xffffffff;
        }
        this.content[this.nextId] = val;
        return this.nextId++;
      }
      this.remove = function(id) {
        const val = this.content[id];
        delete this.content[id];
        return val;
      }
    }

    const _root = Function('return this')();
    const _objects = new HandleMap();
    _objects.add({o: _root, n: 1}) // key: id, value: object
    const _storages = new HandleMap();

    const kTypeUndefined = 0;
    const kTypeBool = 1;
    const kTypeInt = 2;
    const kTypeString = 3;
    const kTypeList = 4;
    const kTypeDict = 5;
    const kTypeObject = 6;
    const kTypeFunc = 7;
    const kTypeArray = 8;

    function exportObject(obj) {
      for (const id in _objects.content) {
        if (_objects.content[id].o == obj) {
          _objects.content[id].n++;
          return id;
        }
      }
      return _objects.add({o: obj, n: 1});
    }

    function makeStorage() {
      return {stubs: [], objects: [], strings: [], arrays: []};
    }

    function closeStorage(storage) {
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
        _js_unref(storage.objects[i]);
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
        const dictDepth = Module.HEAPU32[(val >> 2) + 2];
        return (...args) => {
          const ptr = Module._malloc(8 * args.length);
          try {
            const storage = makeStorage();
            try {
              for (let i = 0; i < args.length; ++i) {
                toWasm(storage, args[i], ptr + 8 * i, dictDepth);
              }
              Module.asm.__indirect_function_table.get(callback)(ctx, ptr, args.length);
            } finally {
              closeStorage(storage);
            }
          } finally {
            Module._free(ptr);
          }
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

      } else if ((typeof val) == "boolean") {
        Module.HEAPU32[jsStubPtr >> 2] = kTypeBool;
        Module.HEAPU32[(jsStubPtr >> 2) + 1] = val;

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

    const _js_ref = function(objId) {
      _objects.content[objId].n++;
    }

    const _js_unref = function(objId) {
      if (--_objects.content[objId].n == 0) {
        delete _objects.content[objId];
      }
    }

    const _js_call_async = function(objId, func, args, nArgs, callback, ctx, dictDepth) {
      const funcName = Module.UTF8ArrayToString(Module.HEAPU8, func);
      const argList = [...Array(nArgs).keys()].map((i) => fromWasm(args + 8 * i));

      const cb = (result, error) => {
        const resultStub = Module._malloc(8);
        try {
          const errorStub = Module._malloc(8);
          try {
            const storage = makeStorage();
            try {
              toWasm(storage, result, resultStub, dictDepth);
              toWasm(storage, error, errorStub, dictDepth);
              Module.asm.__indirect_function_table.get(callback)(ctx, resultStub, errorStub);
            } finally {
              closeStorage(storage);
            }
          } finally {
            Module._free(errorStub);
          }
        } finally {
          Module._free(resultStub);
        }
      };

      _objects.content[objId].o[funcName](...argList).then(
        (result) => cb(result, undefined),
        (error) => { console.log(error); cb(undefined, error)}
      );
    }

    const _js_get_property = function(objId, property, dictDepth, pOut) {
      const propName = Module.UTF8ArrayToString(Module.HEAPU8, property);
      const result = _objects.content[objId].o[propName];
      const storage = makeStorage();
      toWasm(storage, result, pOut, dictDepth)
      return _storages.add(storage);
    }

    const _js_set_property = function(objId, property, arg) {
      const propName = Module.UTF8ArrayToString(Module.HEAPU8, property);
      _objects.content[objId].o[propName] = fromWasm(arg);
    }

    const _js_release = function(storage) {
      closeStorage(_storages.remove(storage));
    }

    __js_ref = _js_ref;
    __js_unref = _js_unref;
    __js_get_property = _js_get_property;
    __js_set_property = _js_set_property;
    __js_call_async = _js_call_async;
    __js_release = _js_release;
  },

  _js_ref: function() {},
  _js_ref__deps: ['$method_support'],
  _js_unref: function() {},
  _js_unref__deps: ['$method_support'],
  _js_get_property: function() {},
  _js_get_property__deps: ['$method_support'],
  _js_set_property: function() {},
  _js_set_property__deps: ['$method_support'],
  _js_call_async: function() {},
  _js_call_async__deps: ['$method_support'],
  _js_release: function() {},
  _js_release__deps: ['$method_support'],
});
