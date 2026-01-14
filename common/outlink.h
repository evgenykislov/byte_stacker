#ifndef OUTLINK_H
#define OUTLINK_H

#include <cstdint>
#include <list>
#include <string>
#include <utility>

#include <boost/asio.hpp>

#include "../common/data.h"

struct AddressPortPoint {
  std::string Address;
  uint16_t Port;
};


class TrunkLink;

/*! \class IOutLink класс для управления внешними tcp-соединениями.
Экземпляр создаётся либо на основе подключенного сокета, либо на основе точки
подключения (и тогда соединение устанавливается в процессе).
Механизм работы:
- создаём экземпляр;
- проводим дополнительные регистрации, назначение обработчиков;
- запускаем подключение/чтение на сокете: вызываем метод Run(); */
class OutLink {
 public:
  /*! Конструктор экземпляра на основе сокета с установленным соединением
  \param socket сокет с соединением */
  OutLink(boost::asio::ip::tcp::socket&& socket);

  /*! Конструктор экземпляра на основе адреса и порта (например mysite.com:80).
  Подключение выполняется автоматически (и асинхронно) при вызове функции Run.
  Если подключение неуспешно (нет адреса, сервера и т.д.), то вызывается явно
  функция отключения для hoster-а.
  \param ctx сетевой контекст бибилиотеки asio
  \param address адрес для подключения. Может быть как явный ip-адрес, так и имя
  сайта
  \param port порт для подключения */
  OutLink(boost::asio::io_context& ctx, std::string address, uint16_t port);

  OutLink(OutLink&& arg) = default;
  OutLink& operator=(OutLink&& arg) = default;
  virtual ~OutLink();

  /*! Запуск подключения в работу. Функция неблокирующая
  \param hoster указатель на "хостера", который работает со всеми подключениями.
  Указатель должен быть корректным, пока идёт работе подлключения
  \param cnr идентификатор этого подключения для идентификации данных */
  void Run(TrunkLink* hoster, ConnectID cnt);

  // TODO Descr
  void SendData(const void* data, size_t data_size);

 private:
  OutLink() = delete;
  OutLink(const OutLink&) = delete;
  OutLink& operator=(const OutLink&) = delete;

  static const size_t kChunkSize = 800;

  boost::asio::ip::tcp::socket socket_;  //! Сокет подключения
  boost::asio::ip::tcp::resolver resolver_;
  char read_buffer_[kChunkSize];  //! Буфер для приёма данных. Однопоточный
  std::string host_;
  std::string service_;
  std::list<boost::asio::ip::tcp::endpoint> resolved_points_;
  TrunkLink* hoster_;
  ConnectID selfid_;


  /*! Функция запрос чтения данных. Функция асинхронная, данные запрашиваются и
  функция сразу завершает работу. Для каждого экземпляра подключения функция
  должна вызываться "однопоточно" */
  void RequestRead();

  /*! Функция запроса подключения к первой точке (от резолвинга) */
  void RequestConnect();
};


#endif  // OUTLINK_H
