#include <vector>
#include <unordered_map>

template <typename T>
class HybridHash {
public:

    std::size_t size() const
    {
        return countUsed() + d_hash.size();
    }

    void reserve(std::size_t size)
    {
        d_vec.resize(size);
    }

    T* find(uint32_t id)
    {
        if (id > 0 && id < d_vec.size()) {
            if (d_vec[id].entity.id == id) {
                return &d_vec[id];
            }
            return nullptr;
        }

        auto it = d_hash.find(id);
        if (it == d_hash.end())
            return nullptr;

        return &it->second;
    }

    T* end() const
    {
        return nullptr;
    }

    T& at(uint32_t id)
    {
        if (id > 0 && id < d_vec.size()) {
            return d_vec[id];
        } 

        return d_hash[id];
    }

    T& operator [](uint32_t id)
    {
        return at(id);
    }


private:

    std::size_t countUsed() const
    {
        std::size_t count = 0;
        for(std::size_t i = 0; i < d_vec.size(); ++i) {
            if (d_vec[i].entity.id == i)
               ++count; 
        }

        return count;
    }

    std::vector<T> d_vec;
    std::unordered_map<uint32_t, T> d_hash;
};

