#include <curl/curl.h>
#include <iostream>
#include <string>

size_t write_callback(char* ptr, size_t size, size_t nmemb, void* userdata) {
    std::string* html = static_cast<std::string*>(userdata);
    html->append(ptr, size * nmemb);
    return size * nmemb;
}

int main() {
    curl_global_init(CURL_GLOBAL_DEFAULT);
    CURL* curl = curl_easy_init();

    if (curl) {
        std::string html;
        curl_easy_setopt(curl, CURLOPT_URL, "https://www.google.com");
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &html);

        CURLcode res = curl_easy_perform(curl);
        if (res == CURLE_OK) {
            std::cout << html << "\n";
        } else {
            std::cerr << "Error: " << curl_easy_strerror(res) << "\n";
        }

        curl_easy_cleanup(curl);
    }

    curl_global_cleanup();
}
