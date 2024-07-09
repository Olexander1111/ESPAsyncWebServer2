#pragma once
#include <Arduino.h>
#include <Print.h>
#include <StringUtils.h>
#include <ESPAsyncWebServer.h>
#include <GSON.h>

constexpr const char* JSON_MIMETYPE = "application/json";

class Move {
public:
    const char* str;
    gson::Parser parser;

    Move() : str(nullptr), parser() {}

    // Move constructor
    Move(Move&& p) noexcept : str(p.str), parser(std::move(p.parser)) {
        p.str = nullptr;
    }

    // Move assignment operator
    Move& operator=(Move&& p) noexcept {
        if (this != &p) {
            str = p.str;
            parser = std::move(p.parser);
            p.str = nullptr;
        }
        return *this;
    }
};


class ChunkPrint : public Print {
private:
    uint8_t* _destination;
    size_t _to_skip;
    size_t _to_write;
    size_t _pos;
public:
    ChunkPrint(uint8_t* destination, size_t from, size_t len)
        : _destination(destination), _to_skip(from), _to_write(len), _pos{ 0 } {}
    virtual ~ChunkPrint() {}
    size_t write(uint8_t c) {
        if (_to_skip > 0) {
            _to_skip--;
            return 1;
        } else if (_to_write > 0) {
            _to_write--;
            _destination[_pos++] = c;
            return 1;
        }
        return 0;
    }
    size_t write(const uint8_t *buffer, size_t size) {
        return this->Print::write(buffer, size);
    }
};

class AsyncJsonResponse : public AsyncAbstractResponse {
protected:
    gson::string _jsonBuffer;
    bool _isValid;

public:
    AsyncJsonResponse() : _isValid{ false } {
        _code = 200;
        _contentType = JSON_MIMETYPE;
    }

    ~AsyncJsonResponse() {}
    gson::string &getRoot() { return _jsonBuffer; }
    bool _sourceValid() const { return _isValid; }
    size_t setLength() {
        _contentLength = _jsonBuffer.s.length();
        if (_contentLength) { _isValid = true; }
        return _contentLength;
    }

    size_t getSize() { return _jsonBuffer.s.length(); }

    size_t _fillBuffer(uint8_t *data, size_t len) {
        ChunkPrint dest(data, _sentLength, len);
        dest.write(reinterpret_cast<const uint8_t*>(_jsonBuffer.s.c_str()), _jsonBuffer.s.length());
        return len;
    }
};

typedef std::function<void(AsyncWebServerRequest *request, gson::Entry &json)> ArJsonRequestHandlerFunction;
typedef std::function<void(AsyncWebServerRequest *request, gson::string &json)> ArJsonRequestHandlerFunction2;

class AsyncCallbackJsonWebHandler : public AsyncWebHandler {
private:
    const String _uri;
    WebRequestMethodComposite _method;
    ArJsonRequestHandlerFunction _onRequest;

    size_t _contentLength;
    size_t _maxContentLength;
    void* _tempObject;
    size_t _tempObjectSize;

public:
    AsyncCallbackJsonWebHandler(const String& uri, ArJsonRequestHandlerFunction onRequest) 
        : _uri(uri), _method(HTTP_POST | HTTP_PUT | HTTP_PATCH), _onRequest(onRequest), _maxContentLength(16384), _tempObject(nullptr), _tempObjectSize(0) {}

    void setMethod(WebRequestMethodComposite method) { _method = method; }
    void setMaxContentLength(int maxContentLength) { _maxContentLength = maxContentLength; }
    void onRequest(ArJsonRequestHandlerFunction fn) { _onRequest = fn; }

    virtual bool canHandle(AsyncWebServerRequest *request) override final {
        if (!_onRequest)
            return false;

        if (!(_method & request->method()))
            return false;

        if (_uri.length() && (_uri != request->url() && !request->url().startsWith(_uri + "/")))
            return false;

        if (!request->contentType().equalsIgnoreCase(JSON_MIMETYPE))
            return false;

        request->addInterestingHeader("ANY");
        return true;
    }

  virtual void handleUpload(AsyncWebServerRequest *request, const String& filename, size_t index, uint8_t *data, size_t len, bool final) override final {}

  virtual void handleBody(AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) override final {
        //Serial.printf("Handling body: index = %d, total = %d, len = %d\n", index, total, len);

        if (_onRequest) {
            _contentLength = total;
            if (total > 0 && _tempObject == nullptr && total < _maxContentLength) {
                _tempObject = malloc(total);
                _tempObjectSize = total;
          //      Serial.println("Allocated memory for temporary object.");
            }
            if (_tempObject != nullptr) {
                memcpy(static_cast<uint8_t*>(_tempObject) + index, data, len);
            //    Serial.printf("Copied %d bytes to temporary object.\n", len);
            } else {
                //Serial.println(F("Failed to allocate memory for temporary object or total size exceeds max limit."));
            }
        }
    }
    virtual bool isRequestHandlerTrivial() override final { return _onRequest ? false : true; }
};


class AsyncCallbackJsonWebHandler2 : public AsyncWebHandler {
private:
    const String _uri;
    WebRequestMethodComposite _method;
    ArJsonRequestHandlerFunction2 _onRequest2;

    size_t _contentLength;
    size_t _maxContentLength;
    void* _tempObject;
    size_t _tempObjectSize;

public:
    AsyncCallbackJsonWebHandler2(const String& uri, ArJsonRequestHandlerFunction2 onRequest) 
        : _uri(uri), _method(HTTP_POST | HTTP_PUT | HTTP_PATCH), _onRequest2(onRequest), _maxContentLength(16384), _tempObject(nullptr), _tempObjectSize(0) {}
    void setMethod(WebRequestMethodComposite method) { _method = method; }
    void setMaxContentLength(int maxContentLength) { _maxContentLength = maxContentLength; }
    void onRequest2(ArJsonRequestHandlerFunction2 fn) { _onRequest2 = fn; }


    virtual bool canHandle(AsyncWebServerRequest *request) override final {
        if (!_onRequest2)
            return false;

        if (!(_method & request->method()))
            return false;

        if (_uri.length() && (_uri != request->url() && !request->url().startsWith(_uri + "/")))
            return false;

        if (!request->contentType().equalsIgnoreCase(JSON_MIMETYPE))
            return false;

        request->addInterestingHeader("ANY");
        return true;
    }

virtual void handleRequest(AsyncWebServerRequest *request) override final {
    if (_onRequest2) {
        if (_tempObject != nullptr && _tempObjectSize > 0) {
            // Create a unique pointer for the temporary object to manage its memory automatically
            std::unique_ptr<char[]> tempObject(static_cast<char*>(_tempObject));
            
            // Create a gson::string to hold the raw JSON data
            gson::string rawJson;
            rawJson.addTextRaw(tempObject.get(), _tempObjectSize);
            _onRequest2(request, rawJson);
            
            // Reset tempObject pointer to release the memory
            _tempObject = nullptr;
            _tempObjectSize = 0;
        } else {
            // No temporary object to process
            request->send(_contentLength > _maxContentLength ? 413 : 400);
        }
    } else {
        // No request handler defined
        request->send(500);
    }
}

    virtual void handleUpload(AsyncWebServerRequest *request, const String& filename, size_t index, uint8_t *data, size_t len, bool final) override final {}

  virtual void handleBody(AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) override final {
        //Serial.printf("Handling body: index = %d, total = %d, len = %d\n", index, total, len);

        if (_onRequest2) {
            _contentLength = total;
            if (total > 0 && _tempObject == nullptr && total < _maxContentLength) {
                _tempObject = malloc(total);
                _tempObjectSize = total;
                //Serial.println("Allocated memory for temporary object.");
            }
            if (_tempObject != nullptr) {
                memcpy(static_cast<uint8_t*>(_tempObject) + index, data, len);
                //Serial.printf("Copied %d bytes to temporary object.\n", len);
            } else {
                //Serial.println("Failed to allocate memory for temporary object or total size exceeds max limit.");
            }
        }
    }
    virtual bool isRequestHandlerTrivial() override final { return _onRequest2 ? false : true; }
};



