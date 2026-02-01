#pragma once
#ifndef SINGLETON_H
#define SINGLETON_H

namespace core {

    // Base template class for Singleton pattern
    /**
     * @class Singleton
     * @brief 单例模式基类模板。
     *
     * 该模板类实现了线程安全的懒汉式单例模式（Meyers' Singleton）。
     * 继承该类的子类将自动成为单例。
     *
     * <h2>使用示例</h2>
     * @code
     * class MyManager : public core::Singleton<MyManager> {
     *     friend class core::Singleton<MyManager>;
     * private:
     *     MyManager() = default; // 构造函数私有化
     * public:
     *     void doSomething() { ... }
     * };
     * 
     * // 使用
     * MyManager::GetInstance().doSomething();
     * @endcode
     */
    template <typename T>
    class Singleton {
    public:
        // Provides global access to the single instance
        // C++11 guarantees thread-safe initialization for static locals
        static T& GetInstance() {
            // The instance is created on first call and lives until program end.
            // Requires that T has a private/protected constructor and
            // has befriended Singleton<T>.
            static T instance;
            return instance;
        }

    protected:
        // Protected constructor: Prevents direct instantiation of Singleton<T> itself,
        // but allows derived class T to call it if needed (though usually T's own
        // constructor is sufficient).
        Singleton() = default;

        // Protected virtual destructor: Good practice for base classes, although
        // direct deletion of Singleton<T> pointers shouldn't happen.
        virtual ~Singleton() = default;

    public:
        // Delete copy constructor and copy assignment operator for the base.
        // The derived class T MUST also delete these for itself.
        Singleton(const Singleton&) = delete;
        Singleton& operator=(const Singleton&) = delete;

        // Delete move constructor and move assignment operator for the base.
        // The derived class T MUST also delete these for itself.
        Singleton(Singleton&&) = delete;
        Singleton& operator=(Singleton&&) = delete;
    };

}

#endif // SINGLETON_H