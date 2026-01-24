#ifndef OUTLINK_H
#define OUTLINK_H

#include <atomic>
#include <cstdint>
#include <list>
#include <map>
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
  void SendData(uint32_t chunk_id, const void* data, size_t data_size);

 private:
  OutLink() = delete;
  OutLink(const OutLink&) = delete;
  OutLink& operator=(const OutLink&) = delete;

  static const size_t kChunkSize = 800;
  static const size_t kMaxChunkAmount = 5000;

  boost::asio::ip::tcp::socket socket_;  //! Сокет подключения
  boost::asio::ip::tcp::resolver resolver_;
  char read_buffer_[kChunkSize];  //! Буфер для приёма данных. Однопоточный
  std::string host_;
  std::string service_;
  std::list<boost::asio::ip::tcp::endpoint> resolved_points_;
  TrunkLink* hoster_;
  ConnectID selfid_;

  // Система выдачи данных наружу

  /*! Финальный буфер для выдачи данных в сетевой сокет. Буфер работает в
  квази-однопоточном режиме: только одна функция в каждый момент работает с
  буфером */
  std::vector<uint8_t> network_write_buffer_;

  /*! Флаг, что нельзя начинать новую операцию записи: она сейчас уже
  "в процессе", или ещё не открыты сокеты */
  std::atomic_flag network_write_operation_;

  /*! Отдельные чанки для сборки полноценного блока данных */
  std::map<uint32_t, std::vector<uint8_t>> write_chunks_;

  /*! Идентификатор пакета, который сейчас валиден и ожидается */
  uint32_t next_write_chunk_id_;

  std::mutex write_chunks_lock_;

  void FillNetworkBuffer();


  /*! Функция запрос чтения данных. Функция асинхронная, данные запрашиваются и
  функция сразу завершает работу. Для каждого экземпляра подключения функция
  должна вызываться "однопоточно" */
  void RequestRead();

  /*! Функция запроса подключения к первой точке (от резолвинга) */
  void RequestConnect();


  // TODO Descr
  void RequestWrite();
};


#endif  // OUTLINK_H
