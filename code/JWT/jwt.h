#ifndef JWT_CPP_JWT_H
#define JWT_CPP_JWT_H

#include <vector>
#include <chrono>
#include <time.h>
#include <string>
#include <string.h> 
#include <unordered_map>

#include <openssl/bio.h> // base64UrlEncode
#include <openssl/evp.h> // base64UrlEncode
#include <openssl/buffer.h>

#include <openssl/hmac.h> // hmacSha256
#include <openssl/pem.h>  // hmacSha256

#include "../Log/log.h"
#include "../Common/commonConfig.h"
#include "../Common/picojson.h"

// JWT 类
// 1. 颁发 Token 
// 2. 验证 Token 是否正确或过期了
class JWT{
private : 
    const char *secret_ ;
    const int expSeconds_ ; 
public :
    
    JWT(const char * secret , const int exp_seconds) : secret_(secret) , expSeconds_(exp_seconds) {}
    ~JWT() = default ;

    std::string generateJWT(const std::unordered_map<std::string , std::string> &keyValue) {
        std::string header = R"({"alg":"HS256","typ":"JWT"})" ; 
        std::string encoded_header = base64UrlEncode(header); 

        picojson::object json ; 
        for(auto iter : keyValue) {
            json[iter.first] = picojson::value(iter.second) ; 
        }
        
        // 定义过期时间 
        int64_t now_timestamp = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
        json["exp"] = picojson::value(now_timestamp + expSeconds_) ; 
         
        // 将 picojson 对象转换为字符串
        std::string payload = picojson::value(json).serialize(); 
        std::string encoded_payload = base64UrlEncode(payload);
        std::string signature = hmacSha256(encoded_header + "." + encoded_payload, secret_);
        std::string token = encoded_header + "." + encoded_payload + "." + base64UrlEncode(signature);
        return token ; 
    } 

    bool judgeJWT(const std::string &token){
        std::vector<std::string> parts = jwt_split(token, ".");
        if (parts.size() != 3) {
            LOG_WARN("Invalid token format");
            return false ; 
        }

        std::string expected_signature = base64UrlEncode(hmacSha256(parts[0] + "." + parts[1], secret_)) ;
        if (parts[2] != expected_signature) { 
            LOG_WARN("Invalid signature");
            return false ; 
        }

        std::string decoded_claims = base64UrlDecode(parts[1]);
        picojson::value json_Value ;
        std::string err = picojson::parse(json_Value , decoded_claims) ; 
        if(!err.empty()){
            LOG_WARN("picojson error %s" , err);
            return false ; 
        }

        if(json_Value.get("exp").to_str().empty()){
            LOG_WARN("Missing expiration time");
            return false ; 
        } 
        return true ; 
    }

    std::string parseJWT(const std::string& token , const std::string &key) {
        
        std::vector<std::string> parts = jwt_split(token, ".");
        if (parts.size() != 3) {
            LOG_WARN("Invalid token format");
            return "" ; 
        }

        std::string expected_signature = base64UrlEncode(hmacSha256(parts[0] + "." + parts[1], secret_)) ;
        if (parts[2] != expected_signature) { 
            LOG_WARN("Invalid signature");
            return "" ; 
        }

        std::string decoded_claims = base64UrlDecode(parts[1]);
        picojson::value json_Value ;
        std::string err = picojson::parse(json_Value , decoded_claims) ; 
        if(!err.empty()){
            LOG_WARN("picojson error %s" , err);
            return "" ; 
        }

        if(json_Value.get("exp").to_str().empty()){
            LOG_WARN("Missing expiration time");
            return "" ; 
        } 
         
        int64_t exp_timestamp = static_cast<int64_t>(json_Value.get("exp").get<int64_t>() ) ; 
        int64_t now_timestamp = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
        
        if (exp_timestamp < now_timestamp) {
            LOG_WARN("Token has expired");
            return "" ; 
        }
        
        // 遍历JSON对象并输出所有的键和值
        if (json_Value.is<picojson::object>()) {
            const picojson::object& obj = json_Value.get<picojson::object>();
            for (picojson::object::const_iterator it = obj.begin(); it != obj.end(); ++it) {
                if (it->second.is<std::string>()) {
                    // std::cout << it->second.get<std::string>() << std::endl;
                    if(it->first == key){
                        return it->second.get<std::string>() ; 
                    } 
                }
            }
        }
        return "" ;
    }
private :
    // base64Url 加密
    std::string base64UrlEncode(const std::string& input) {
        BIO* b64 = BIO_new(BIO_f_base64());
        if (!b64) {
            LOG_ERROR("Failed to create base64 encoder BIO") ; 
            return "" ;  
        }
        BIO_set_flags(b64, BIO_FLAGS_BASE64_NO_NL);
        BIO* bmem = BIO_new(BIO_s_mem());
        if (!bmem) {
            BIO_free_all(b64); LOG_ERROR("Failed to create memory BIO") ; 
            return "" ;   
        }

        b64 = BIO_push(b64, bmem);
        BIO_write(b64, input.c_str(), input.length());
        BIO_flush(b64);
        BUF_MEM* bptr; BIO_get_mem_ptr(b64, &bptr); 
        
        // Base64 URL 编码的特殊处理
        std::string output ; 
        for(size_t i = 0 ; i < bptr->length ; ++i){
            if(bptr->data[i] == '+'){
                output.push_back('-') ; 
            }else if(bptr->data[i] == '/') {
                output.push_back('_') ; 
            }else if(bptr->data[i] == '=') {
                continue ; 
            }else {
                output.push_back(bptr->data[i]) ;
            }
        }
        BIO_free_all(b64);
        return output;
    }
    // base64Url 解密
    std::string base64UrlDecode(const std::string& input) {
        // First, replace '-' with '+' and '_' with '/'
        std::string base64 = input;
        std::replace(base64.begin(), base64.end(), '-', '+');
        std::replace(base64.begin(), base64.end(), '_', '/');

        // Add padding characters as needed
        switch (base64.length() % 4) {
            case 2:
                base64 += "==";
                break;
            case 3:
                base64 += "=";
                break;
        }

        // Create a memory BIO and base64 decode filter
        BIO* b64 = BIO_new(BIO_f_base64());
        BIO* mem = BIO_new_mem_buf(base64.data(), base64.length());
        BIO_push(b64, mem);
        BIO_set_flags(b64, BIO_FLAGS_BASE64_NO_NL);

        // Decode the data
        std::string decoded;
        char buffer[1024];
        int len;
        while ((len = BIO_read(b64, buffer, sizeof(buffer))) > 0) {
            decoded.append(buffer, len);
        }

        // Clean up
        BIO_free_all(b64);
        return decoded;
    }

    // 计算 HMAC-SHA256
    std::string hmacSha256(const std::string& message, const char *key) {
        HMAC_CTX* ctx = HMAC_CTX_new();
        HMAC_Init_ex(ctx, key , strlen(key) , EVP_sha256(), nullptr);
        HMAC_Update(ctx, reinterpret_cast<const unsigned char*>(message.data()), message.size());
        unsigned char digest[EVP_MAX_MD_SIZE];
        unsigned int length = EVP_MAX_MD_SIZE;
        HMAC_Final(ctx, digest, &length);
        HMAC_CTX_free(ctx);
        return std::string(reinterpret_cast<const char*>(digest), length);
    }
    
    std::vector<std::string> jwt_split(const std::string& str, const std::string& delimiter) {
        std::vector<std::string> parts;
        size_t start = 0;
        size_t end = str.find(delimiter, start);
        while (end != std::string::npos) {
            parts.push_back(str.substr(start, end - start));
            start = end + delimiter.size();
            end = str.find(delimiter, start);
        }
        parts.push_back(str.substr(start));
        return parts;
    }
} ; 
    
    

    

 
#endif
