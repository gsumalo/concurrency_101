#include <atomic>
#include <cctype>
#include <condition_variable>
#include <iostream>
#include <mutex>
#include <regex>
#include <thread>

struct BrowsingEvent
{
    BrowsingEvent() = default;
    BrowsingEvent(const BrowsingEvent &) = default;
    BrowsingEvent(BrowsingEvent &&) = default;
    BrowsingEvent(const std::string & device, const std::string & url, const std::string & timestamp)
        : m_device(device), m_url(url), m_timestamp(timestamp)
    {
    }

    BrowsingEvent & operator=(const BrowsingEvent &) = default;
    BrowsingEvent & operator=(BrowsingEvent &&) = default;

    std::string m_device;
    std::string m_url;
    std::string m_timestamp;
};

class ConcurrentQueue
{
public:
    ConcurrentQueue()
        : m_is_closed_(false)
    {        
    }

    void close_queue()
    {
        std::unique_lock<std::mutex> safe(m_guard_);

        m_is_closed_ = true;
        m_signaller_.notify_all();
    }

    bool pull_one(BrowsingEvent & event)
    {
        std::unique_lock<std::mutex> safe(m_guard_);

        bool rv(false);

        if (!m_events_.empty()) {
            event = m_events_.back();
            m_events_.pop_back();
            rv = true;
        } else {
            while (!m_is_closed_ && m_events_.empty()) {
                m_signaller_.wait(safe);
            }

            if (!m_events_.empty()) {
                event = m_events_.back();
                m_events_.pop_back();
                rv = true;
            }
        }

        return rv;
    }

    void push(const BrowsingEvent & event)
    {
        std::unique_lock<std::mutex> safe(m_guard_);

        m_events_.push_back(event);
        m_signaller_.notify_all();
    }
    
private:
    std::condition_variable_any m_signaller_;
    std::mutex m_guard_;
    bool m_is_closed_;
    std::vector<BrowsingEvent> m_events_;   
};

class Storage
{
public:
    Storage()
        : m_count_(0)
    {        
    }

    uint64_t get_count() const
    {
        uint64_t rv(m_count_);
        return rv;
    }

    void increase_count()
    {
        ++m_count_;
    }
private:
    std::atomic<uint64_t> m_count_;
};

class Producer
{
public:
    Producer(std::istream & input, ConcurrentQueue & queue)
        : m_input_(input), m_queue_(queue)
    {
    }

    void publish(const std::string & device, const std::string & url, const std::string & timestamp)
    {
        m_queue_.push(BrowsingEvent(device, url, timestamp));
    }

    void run()
    {
        std::string first_line, second_line, third_line;
        std::string device, url, timestamp;

        while (!m_input_.eof()) {
            std::getline(m_input_, first_line);
            
            if (!is_valid_device_(first_line)) {
                continue;
            }

            std::getline(m_input_, second_line);

            if (!is_valid_url_(second_line)) {
                continue;

            }

            std::getline(m_input_, third_line);

            if (!is_valid_timestamp_(third_line)) {
                continue;
            }

            publish(first_line, second_line, third_line);
        }

        m_queue_.close_queue();
    }

private:
    static bool is_valid_device_(const std::string & str)
    {
        static const std::regex valid_pattern("device: [[:alnum:]]{2}(\\-[[:alnum:]]{2}){5}");

        bool rv(std::regex_match(str, valid_pattern));
        return rv;
    }

    static bool is_valid_timestamp_(const std::string & str)
    {
        static const std::regex valid_pattern("timestamp: [[:digit:]]+");

        bool rv(std::regex_match(str, valid_pattern));
        return rv;
    }

    static bool is_valid_url_(const std::string & str)
    {
        static const std::regex valid_pattern("url: [^:]+://[^/]+(:[[:digit:]]+)?(/(.*))?");

        bool rv(std::regex_match(str, valid_pattern));
        return rv;
    }

    std::istream & m_input_;
    ConcurrentQueue & m_queue_;
};

class Consumer
{
public:
    Consumer(Storage & storage, ConcurrentQueue & queue)
        : m_storage_(storage), m_queue_(queue)
    {
    }

    bool is_questionable(const BrowsingEvent & event) const
    {
        static const std::vector<std::string> m_offending_words_ = {
            "porn",
            "xxx",
            "sex",
            "Bieber",
        };

        bool rv(false);

        auto domain(get_domain(event.m_url));

        // Look for offending words
        for (const auto & word: m_offending_words_) {
            // Insensitive case comparison
            auto found(std::search(domain.begin(), domain.end(), word.cbegin(), word.cend(), 
                    [](unsigned char one_value, unsigned char other_value) -> bool {
                        bool rv(std::tolower(one_value) == std::tolower(other_value));
                        return rv;
                    }));

            if (found != domain.end()) {
                rv = true;
                break;
            }
        }

        return rv;
    }

    void run()
    {
        BrowsingEvent my_event;

        while(m_queue_.pull_one(my_event)) {
            if (is_questionable(my_event)) {
                m_storage_.increase_count();
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }
        }
    }

private:
    static std::string get_domain(const std::string & url_input)
    {
        static const std::regex general_split_pattern("url: [^:]+://([^/]+)(/(.*))?");
        static const std::regex authority_split_pattern("([^@]+@)?([^:]+)(:[[:digit:]]+)?");

        std::string rv;

        std::string authority;
        std::match_results<std::string::const_iterator> general_sub_matches;
        std::match_results<std::string::iterator> authority_sub_matches;

        if (std::regex_match(url_input.begin(), url_input.end(), general_sub_matches, general_split_pattern) && (general_sub_matches.size() >= 2)) {
            authority.assign(general_sub_matches[1].first, general_sub_matches[1].second);
        }

        if (std::regex_match(authority.begin(), authority.end(), authority_sub_matches, authority_split_pattern) && (authority_sub_matches.size() >= 3)) {

            rv.assign(authority_sub_matches[2].first, authority_sub_matches[2].second);
        }

        return rv;
    }

    Storage & m_storage_;
    ConcurrentQueue & m_queue_;
};


int main() {
    ConcurrentQueue communicator;
    Storage storage;
    Producer producer(std::cin, communicator);

    Consumer consumer1(storage, communicator);
    Consumer consumer2(storage, communicator);
    std::thread t1(&Consumer::run, &consumer1);  
    std::thread t2(&Consumer::run, &consumer2);

    producer.run();
    
    t1.join();
    t2.join();

    std::cout << storage.get_count();

    return 0;
}