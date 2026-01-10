#ifndef PARSER_H
#define PARSER_H

#include <string>
#include <utility>

#include <boost/asio.hpp>

/*! Разбор аргумента командной строки как точки коннекта
\param arg_wo_prefix аргумент без префикса, например 3=127.0.0.1:123
\param id номер точки коннекта (выход)
\param address адрес точки коннекта (выход)
\param port порт точки окннекта (выход)
\return признак, что разбор был успешен */
bool ParsePoint(std::string arg_wo_prefix, unsigned int& id,
    std::string& address, uint16_t& port);

/*! Разбор аргумента командной строки как точки коннекта в формате ip4
\param arg_wo_prefix аргумент без префикса, например 3=127.0.0.1:123
\param id номер точки коннекта (выход)
\param ednpoint точка подключения, преобразованная в формат ip4 (выход)
\return признак, что разбор был успешен */
bool ParsePoint(std::string arg_wo_prefix, unsigned int& id,
    boost::asio::ip::tcp::endpoint& point);

/*! Разбор аргумента командной строки как транковой точки коннекта
\param arg_wo_prefix аргумент без префикса, например 127.0.0.1:123,456
\param points массив точек коннекта в формате ip4
\return признак, что разбор был успешен */
bool ParseTrunkPoint(std::string arg_wo_prefix,
    std::vector<boost::asio::ip::udp::endpoint>& points);

#endif  // PARSER_H
