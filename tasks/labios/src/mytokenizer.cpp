// // mytokenizer.cpp

// #include <chimaera/api/chimaera_client.h>
// #include <hermes_shm/hermes_shm.h>
// #include <string>
// #include <vector>
// #include <iostream>
// #include <sstream>
// #include <iomanip>
// #include <cstring>

// namespace labios {

// /**
//  * Takes raw data and tokenizes it into a string representation
//  * 
//  * @param data Pointer to the raw data
//  * @param data_size Size of the data in bytes
//  * @return String representation of the tokenized data
//  */
// std::string mytokenizer(const char* data, size_t data_size) {
//     // Simple implementation: convert binary data to hex string
//     std::stringstream ss;
    
//     // Add a header with size information
//     ss << "TOKEN_HEADER:" << data_size << ":";
    
//     // Convert each byte to hex representation
//     for (size_t i = 0; i < data_size; ++i) {
//         ss << std::setfill('0') << std::setw(2) << std::hex 
//            << static_cast<int>(static_cast<unsigned char>(data[i]));
        
//         // Add a separator every 32 bytes for readability
//         if ((i + 1) % 32 == 0 && i < data_size - 1) {
//             ss << "\n";
//         }
//     }
    
//     return ss.str();
// }

// } // namespace labios

// std::string mytokenizer(char* data, size_t data_size) {
// // For testing purposes, just return a copy of the data

// // return std::string(data, data_size);
//   std::string result;
//   bool in_token = false;
  
//   for (size_t i = 0; i < data_size; ++i) {
//       char c = data[i];
      
//       // Check if character is whitespace
//       bool is_whitespace = (c == ' ' || c == '\t' || c == '\n' || c == '\r');
      
//       if (!is_whitespace) {
//           // Add character to result
//           result += c;
//           in_token = true;
//       } else if (in_token) {
//           // Add token separator
//           result += "|";
//           in_token = false;
//       }
//   }
  
//   return result;
// }

#include <string>
#include <vector>
#include <unordered_map>
#include <algorithm>

// A simplified subword tokenizer
class SimpleSubwordTokenizer {
private:
    std::unordered_map<std::string, int> vocab;
    const int max_token_length = 10;
    
public:
    SimpleSubwordTokenizer() {
        // Initialize with a basic vocabulary (in practice, this would be much larger)
        // This is a very simplified example
        const std::vector<std::string> basic_tokens = {
            "the", "a", "an", "in", "on", "at", "to", "from", "with", "and", "or", "but",
            "hello", "world", "data", "token", "model", "language", "process", "system",
            "lab", "ios", "ch", "ar", "er", "ing", "ed", "s", "es", "ly", "ment"
        };
        
        for (size_t i = 0; i < basic_tokens.size(); ++i) {
            vocab[basic_tokens[i]] = i;
        }
    }
    
    std::string tokenize(const std::string& text) {
        std::vector<std::string> tokens;
        size_t pos = 0;
        
        while (pos < text.length()) {
            // Find the longest matching token starting at current position
            std::string best_match = "";
            
            // Try different lengths starting from the maximum
            for (int len = std::min(max_token_length, static_cast<int>(text.length() - pos)); len > 0; --len) {
                std::string candidate = text.substr(pos, len);
                if (vocab.find(candidate) != vocab.end()) {
                    best_match = candidate;
                    break;
                }
            }
            
            // If no match found, use character as fallback
            if (best_match.empty()) {
                best_match = text.substr(pos, 1);
            }
            
            tokens.push_back(best_match);
            pos += best_match.length();
        }
        
        // Join tokens with separator
        std::string result;
        for (size_t i = 0; i < tokens.size(); ++i) {
            if (i > 0) {
                result += "|";
            }
            result += tokens[i];
        }
        
        return result;
    }
};

std::string mytokenizer(char* data, size_t data_size) {
    // Convert input data to string
    std::string text(data, data_size);
    
    // Create tokenizer instance
    SimpleSubwordTokenizer tokenizer;
    
    // Tokenize the text
    return tokenizer.tokenize(text);
}