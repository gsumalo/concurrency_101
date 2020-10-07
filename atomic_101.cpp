#include <array>
#include <atomic>
#include <cstdint>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>


class SynchronizedOutput
{
public:
    SynchronizedOutput(std::ostream & output)
        : m_output_(output)
    {
    }
     
    void write(char value)
    {
        std::unique_lock<std::mutex> guard(m_safe_);
        
        m_output_ << value;        
    }
       
private:
    std::ostream & m_output_;
    std::mutex m_safe_;
};

struct PrinterParams
{
public:
    PrinterParams(SynchronizedOutput & output, std::atomic<char> & expected, char assignment)
        : m_output(output), m_expected(expected), m_assignment(assignment)
    {
    }

    bool is_end() const
    {
        bool rv(m_expected == m_end_value);
        return rv;
    }

    bool is_reset()
    {  
        bool rv(m_expected == m_reset_value);
        return rv;
    }

    void reset_current_value()
    {
        m_expected = m_reset_value;
    }
    
    SynchronizedOutput & m_output;
    std::atomic<char> & m_expected;
    char m_assignment;
    
    static const char m_reset_value = '\0';
    static const char m_end_value = 127;
};

class Printer
{
public:
    Printer(PrinterParams & params)
        : m_params_(params)
    {
    }    

    void run()
    {
        while (!m_params_.is_end()) {
            if (m_params_.m_expected == m_params_.m_assignment) {
                m_params_.m_output.write(m_params_.m_assignment);
                m_params_.reset_current_value();
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(25));
        }
    }
    
private:
    PrinterParams & m_params_;
};

int main()
{
    int rv(0);
    
    try {
        SynchronizedOutput output(std::cout);
        std::atomic<char> current_char(PrinterParams::m_reset_value);
        std::array<char,2> expected_chars = { 'A', 'B' };
        
        PrinterParams params_a(output, current_char, expected_chars[0]);
        PrinterParams params_b(output, current_char, expected_chars[1]);
        Printer printer_a(params_a);
        Printer printer_b(params_b);
 
        std::thread agent_a(&Printer::run, &printer_a);
        std::thread agent_b(&Printer::run, &printer_b);
        
        for (uint64_t counter(0); counter < 10000; ++counter) {
            current_char = expected_chars[counter % expected_chars.size()];
            
            while (current_char != PrinterParams::m_reset_value) {
                std::this_thread::sleep_for(std::chrono::milliseconds(25));
            }
        }

        current_char = PrinterParams::m_end_value;

        agent_a.join();
        agent_b.join();
    } catch(...) {
        rv = 1;
    }
    
    return rv;
}
