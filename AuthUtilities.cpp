#include <iostream>
#include <cstdlib>
#include <cstring>
#include <cctype>
#include <ctime>
#include <fstream>
#include <ios>
#include <bits/stdc++.h>
#include <sqlite3.h>

#include "User.hpp"
#include "bcrypt.h"
#include "mainwindow.h"
#include "ui_mainwindow.h"




/* Callback functions */


/*
 * Callback function. If this function is every called, the query will have found a
 * match for the provided email. Of course, that means it is already in use and we need
 * to return with 1 to prevent the OK value.
*/
int EmailInUse(void* data, int argc, char** argv, char** /* azColName */) {
    return 1;
}

/*
 * Callback function that will set the salt to what the query returns.
*/
int GetSalt(void* data, int argc, char** argv, char** /* azColName */) {
    std::vector<std::string *>  user_data = * static_cast<std::vector<std::string *> *>(data);
    if (argc == 0) { // means the query did not get any hits. user/email DNE
        return 1;
    }
    *(user_data.at(0)) = argv[0];
    *(user_data.at(1)) = argv[1];
    *(user_data.at(2)) = argv[2];

    return 0;
}

/*
 * Callback function that will only return the OK value
 * if the password+salt is valid.
*/
int CheckPassword(void* data, int argc, char** argv, char** /* azColName */) {
    auto& [password, salt] = *static_cast<std::pair<std::string, std::string> *>(data);
    // argv[0] is the password hash.
    return !bcrypt::validatePassword(password + salt, argv[0]);
}


/*
 * Function to login a user. It will ask for credentials
 * and compare that to what's stored in the database.
 * The new_user parameter is only set to true if the NewAccount()
 * function is calling this function (NewAccount should redirect to login)
 * It just tells the function there is no saved data to load.
*/

void MainWindow::Login(std::string email, std::string password) {
    // user information
    std::string salt = " ";
    std::string first_name = " ";
    std::string last_name = " ";



    // make email fully lower case
    std::transform(email.begin(), email.end(), email.begin(), [](unsigned char c){ return std::tolower(c); });



    sqlite3* db;
    int rc = sqlite3_open("creds.db", &db);
    if (rc) {
        ui->ErrorTXT->append("Cannot open database: ");
        ui->ErrorTXT->append(sqlite3_errmsg(db));
        return;
    }


    // get the salt for this username
    std::vector<std::string *> userData = {&salt, &first_name, &last_name};
    std::string email_command = "SELECT salt, first_name, last_name FROM credentials WHERE email = '" + email + "';";
    rc = sqlite3_exec(db, email_command.c_str(), GetSalt, &userData, 0); // shallow copy of salt NOT fine
    // if the salt remains empty, that means the email did not have any saved data.
    if (salt == " ") {
        ui->ErrorTXT->append("Invalid Login. Try Again\n");
        sqlite3_close(db);
        return;
    }

    // if we get the salt, get and check the password.
    std::string selectDataSQL = "SELECT password_hash FROM credentials WHERE email = '" + email + "';";
    std::pair<std::string, std::string> tmp = {password, salt}; // shallow copies are fine here
    rc = sqlite3_exec(db, selectDataSQL.c_str(), CheckPassword, &tmp, 0);
    if (rc == SQLITE_OK) {
        // construct a new user object and send back the address
        this->user = new User(first_name, last_name, email, password, salt);
        sqlite3_close(db);
        return;
    }
    ui->ErrorTXT->append("Invalid Login. Try Again\n");
    sqlite3_close(db);
}

/*
 * Function to create a new user account. It will ask for basic user
 * information and have them create a password that follows the rules
 * outlined in ValidPassword(). On success, it will save the provided credentials
 * and redirect the user to the login page.
*/

bool MainWindow::NewAccount(std::string first_name, std::string last_name, std::string email, std::string password) {
    // set up database if not already done so
    sqlite3 * db;
    int rc = sqlite3_open("creds.db", &db);
    if (rc) {
        ui->ErrorSignupTXT->append("Could not open the database");
        return false;
    }

    // create a table if non exists
    const char * create_table = "CREATE TABLE IF NOT EXISTS credentials (email TEXT PRIMARY KEY, password_hash TEXT, salt TEXT, first_name TEXT, last_name TEXT);";
    rc = sqlite3_exec(db, create_table, 0, 0, 0);
    if (rc != SQLITE_OK) {
        ui->ErrorSignupTXT->append("Error creating creds table\n");
        sqlite3_close(db);
        return false;
    }

    // user information
    std::string salt = " ";
    std::string hash = " ";

    // get the email all lowercase
    std::transform(email.begin(), email.end(), email.begin(), [](unsigned char c){ return std::tolower(c); });

    // see if that email is already in the database
    std::string check_email =  "SELECT first_name FROM credentials WHERE email = '" + email + "';";
    int ret = sqlite3_exec(db, check_email.c_str(), EmailInUse, 0, 0);
    if (ret != SQLITE_OK) {
        ui->ErrorSignupTXT->append("That email is already in use\n");
        sqlite3_close(db);
        return false;
    }

    // Get a password and assign a salt. Ensure the password is strong enough.
    if (!ValidPassword(password)) {
        ui->ErrorSignupTXT->append("Choose a stronger password!\n");
        sqlite3_close(db);
        return false;
    }

    salt = GenSalt();
    hash = bcrypt::generateHash(password + salt);

    // Save the hash, salt, and other user data
    std::string insert = "INSERT INTO credentials (email, password_hash, salt, first_name, last_name) VALUES ('" + email + "', " +
                         "'" + hash + "', " +
                         "'" + salt + "', " +
                         "'" + first_name + "', " +
                         "'" + last_name + "');";

    rc = sqlite3_exec(db, insert.c_str(), 0, 0, 0);
    if (rc != SQLITE_OK) {
        ui->ErrorSignupTXT->append("Error saving cred data\n");
        sqlite3_close(db);
        return false;
    }

    std::cout << "Great! You'll be redirected to the login page.\n";
    sqlite3_close(db);
    return true;
}



/* helper functions */

/*
 * Function that will enforces password rules.
*/
bool MainWindow::ValidPassword(std::string password) {
    // check for length
    if (password.size() < 5 || password.size() > 15) {
        ui->ErrorSignupTXT->append("Password must be between 5 and 15 characters long\n");
        return false;
    }
    bool lower_case = false;
    bool upper_case = false;
    bool num = false;
    bool letter = false;
    bool special_char = false;
    for (int i = 0; i < password.size(); i++) {
        char let = password.at(i);
        if (tolower(let) == let) lower_case = true;
        if (toupper(let) == let) upper_case = true;
        if (isdigit(let)) num = true;
        if (isalpha(let)) letter = true;
        if (!isalnum(let)) special_char = true;
    }
    if (!lower_case) {
        ui->ErrorSignupTXT->append("Password must contain at least one lower case letter\n");
        return false;
    } else if (!upper_case) {
        ui->ErrorSignupTXT->append("Password must contain at least one upper case letter\n");
        return false;
    } else if (!num) {
        ui->ErrorSignupTXT->append("Password must contain at least one number\n");
        return false;
    } else if (!letter) {
        ui->ErrorSignupTXT->append("Password must contain at least one letter\n");
        return false;
    } else if (!special_char) {
        ui->ErrorSignupTXT->append("Password must contain at least one special character\n");
        return false;
    }

    return true;
}

/*
 * Function that will generate a salt to pair with a password.
 * note, these random  numbers aren't secure, but that's fine because
 * we can still ensure security if the salt is breached. Also, we store
 * the salt as plaintext next to the password hash, so its not a secret. It
 * is only used to prevent a dictionary/table attack for known plaintext - hash combos.
*/
std::string MainWindow::GenSalt() {
    srand((int) time(0));
    int len = (rand() % 10) + 5; // length will vary from 5 to 14 inclusive
    std::string salt = "";
    for (int i = 0; i < len; i++) {
        char r = (rand() % 94) + 33; // from 33 to 126 inclusive
        salt += (char) r;
    }
    return salt;
}
