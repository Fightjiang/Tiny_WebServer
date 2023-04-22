#include <iostream>
#include "picojson.h"

using namespace std;

int main()
{
    string json_string = R"({"name": "Alice", "age": 20})";  // 原始的JSON字符串

    picojson::value v;  // 解析出来的JSON对象
    string err = picojson::parse(v, json_string);  // 解析JSON字符串，出错时err不为空
    
    if (!err.empty()) {
        cerr << err << endl;
        return 1;
    }

    // 从JSON对象中取出name和age的值
    string name = v.get("name").to_str();
    int age = static_cast<int>(v.get("age").get<double>());

    cout << "name: " << name << ", age: " << age << endl;

    return 0;
}