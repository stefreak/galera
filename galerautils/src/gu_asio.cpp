//
// Copyright (C) 2014-2015 Codership Oy <info@codership.com>
//

#include "gu_config.hpp"
#include "gu_asio.hpp"

#include <boost/bind.hpp>

void gu::ssl_register_params(gu::Config& conf)
{
    // register SSL config parameters
    conf.add(gu::conf::use_ssl);
    conf.add(gu::conf::ssl_verify_peer);
    conf.add(gu::conf::ssl_cipher);
    conf.add(gu::conf::ssl_compression);
    conf.add(gu::conf::ssl_key);
    conf.add(gu::conf::ssl_cert);
    conf.add(gu::conf::ssl_ca);
    conf.add(gu::conf::ssl_password_file);
}

/* checks if all mandatory SSL options are set */
static bool ssl_check_conf(const gu::Config& conf)
{
    using namespace gu;

    bool explicit_ssl(false);

    if (conf.is_set(conf::use_ssl))
    {
        if  (conf.get<bool>(conf::use_ssl) == false)
        {
            return false; // SSL is explicitly disabled
        }
        else
        {
            explicit_ssl = true;
        }
    }

    int count(0);

    count += conf.is_set(conf::ssl_key);
    count += conf.is_set(conf::ssl_cert);

    bool const use_ssl(explicit_ssl || count > 0);

    if (use_ssl && count < 2)
    {
        gu_throw_error(EINVAL) << "To enable SSL at least both of '"
                               << conf::ssl_key << "' and '" << conf::ssl_cert
                               << "' must be set";
    }

    return use_ssl;
}

void gu::ssl_init_options(gu::Config& conf)
{
    bool use_ssl(ssl_check_conf(conf));

    if (use_ssl == true)
    {
        // set defaults

        // cipher list
        const std::string cipher_list(conf.get(conf::ssl_cipher, "AES128-SHA"));
        conf.set(conf::ssl_cipher, cipher_list);

        // peer verification
        bool verify_peer(conf.get(conf::ssl_verify_peer, true));
        if (verify_peer == false)
        {
            log_warn << "disabling SSL peer verification, THIS IS NOT SECURE!";
        }
        conf.set(conf::ssl_verify_peer, verify_peer);

        // compression
        bool compression(conf.get(conf::ssl_compression, true));
        if (compression == false)
        {
            log_info << "disabling SSL compression";
            sk_SSL_COMP_zero(SSL_COMP_get_compression_methods());
        }
        conf.set(conf::ssl_compression, compression);


        // verify that asio::ssl::context can be initialized with provided
        // values
        try
        {
            asio::io_service io_service;
            asio::ssl::context ctx(io_service, asio::ssl::context::sslv23);
            ssl_prepare_context(conf, ctx);
        }
        catch (asio::system_error& ec)
        {
            gu_throw_error(EINVAL) << "Initializing SSL context failed: "
                                   << extra_error_info(ec.code());
        }
    }
}

namespace
{
    // Callback for reading SSL key protection password from file
    class SSLPasswordCallback
    {
    public:
        SSLPasswordCallback(const gu::Config& conf) : conf_(conf) { }

        std::string get_password() const
        {
            std::string   file(conf_.get(gu::conf::ssl_password_file));
            std::ifstream ifs(file.c_str(), std::ios_base::in);

            if (ifs.good() == false)
            {
                gu_throw_error(errno) <<
                    "could not open password file '" << file << "'";
            }

            std::string ret;
            std::getline(ifs, ret);
            return ret;
        }
    private:
        const gu::Config& conf_;
    };
}

void gu::ssl_prepare_context(const gu::Config& conf, asio::ssl::context& ctx,
                             bool verify_peer_cert)
{
    bool verify_peer(conf.get(conf::ssl_verify_peer, true));
    ctx.set_verify_mode((verify_peer == true ?
                         asio::ssl::context::verify_peer : asio::ssl::context::verify_none) |
                        (verify_peer_cert == true ?
                         asio::ssl::context::verify_fail_if_no_peer_cert : 0));
    SSLPasswordCallback cb(conf);
    ctx.set_password_callback(
        boost::bind(&SSLPasswordCallback::get_password, &cb));

    std::string param;

    try
    {
        param = conf::ssl_key;
        ctx.use_private_key_file(conf.get(param), asio::ssl::context::pem);
        param = conf::ssl_cert;
        ctx.use_certificate_file(conf.get(param), asio::ssl::context::pem);
        param = conf::ssl_ca;
        ctx.load_verify_file(conf.get(param, conf.get(conf::ssl_cert)));
        param = conf::ssl_cipher;
        SSL_CTX_set_cipher_list(ctx.impl(), conf.get(param).c_str());
        ctx.set_options(asio::ssl::context::no_sslv2 |
                        asio::ssl::context::no_sslv3 |
                        asio::ssl::context::no_tlsv1);
    }
    catch (asio::system_error& ec)
    {
        gu_throw_error(EINVAL) << "Bad value '" << conf.get(param, "")
                               << "' for SSL parameter '" << param
                               << "': " << extra_error_info(ec.code());
    }
    catch (gu::NotSet& ec)
    {
        gu_throw_error(EINVAL) << "Missing required value for SSL parameter '"
                               << param << "'";
    }
}
