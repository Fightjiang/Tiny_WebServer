#include <iostream>
#include <unordered_map>
#include "picojson.h"

using namespace std;

void test_value(){
    string json_string = R"({"name": "Alice", "age": 20})";  // 原始的JSON字符串

    picojson::value v;  // 解析出来的JSON对象
    string err = picojson::parse(v, json_string);  // 解析JSON字符串，出错时err不为空
    
    if (!err.empty()) {
        cerr << err << endl;
        return  ;
    }

    // 从JSON对象中取出name和age的值
    string name = v.get("name").to_str();
    int age = static_cast<int>(v.get("age").get<double>());

    cout << "name: " << name << ", age: " << age << endl;
}

void test_vec(){
    std::unordered_map<std::string , int> userNames_ ; 
    for(int i = 0 ; i < 4 ; ++i){
        std::string name = "test" + to_string(i) ; 
        userNames_[name] = i ; 
    }
    picojson::object message_json , nameMessage;  
    message_json["isSystem"] = picojson::value(true) ;  
    std::vector<picojson::value> vecName ; 
    for(const auto &iter : userNames_){
        vecName.push_back(picojson::value(iter.first)) ;
    }
    message_json["message"] = picojson::value(vecName) ;

    // 将 picojson 对象转换为字符串
    std::string systemMessage = picojson::value(message_json).serialize(); 
    std::cout<<systemMessage<<std::endl ; 
}

int main()
{
    test_vec() ; 
    return 0;
}