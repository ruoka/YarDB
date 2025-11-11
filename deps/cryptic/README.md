# cryptic
SHA1, SHA2 and BASE64 algorithms header only implementations with C++20.

Usage is straight forward.

Example #1
```c++
auto test1 = "The quick brown fox jumps over the lazy dog"s;
cout << cryptic::sha1::base64(test1) << endl;
```
Example #2
```c++
auto test2 = "The quick brown fox jumps over the lazy dog"s;
cout << cryptic::sha256::hexadecimal(test2) << endl;
```
Example #3
```c++
auto test3 = cryptic::base64::encode("Man"s);
cout << test3 << endl;
```
Example #4
```c++
auto test4 = cryptic::base64::decode(test3);
cout << test4 << endl;
```
