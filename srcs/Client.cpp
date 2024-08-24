#include "Client.hpp"

/*
** --------------------------------- PRIVATE METHODS ---------------------------
*/

// Client::Client(void) : _fd(-1), _socket(NULL), _request(new Request(this)), _response(new Response(this->getRequest()))
// {
// }

Client::Client(int fd, Socket* socket) : _socket(socket), _request(new Request(this)), _response(new Response(this))
{
	Logger::log(Logger::DEBUG, "[Client] Initializing client with fd %d", fd);

	struct sockaddr_in	addr;
	socklen_t			addrLen = sizeof(addr);

	this->_fd = protectedCall(accept(fd, (struct sockaddr *)&addr, &addrLen), "Error with accept function", false);
	protectedCall(fcntl(this->_fd, F_SETFL, O_NONBLOCK), "[Client] Error with fcntl function", false);
}

Client::~Client(void)
{
	if (this->_fd != -1)
		protectedCall(close(this->_fd), "[~Client] Faild to close client socket", false);
	if (this->_request != NULL)
		delete this->_request;
	if (this->_response != NULL)
		delete this->_response;
}

/*
** --------------------------------- HANDLE ---------------------------------
*/

/**
 * @brief Handle the request of the client
 * 
 */
int	Client::handleRequest( int epollFD )
{
	if (this->_request->getState() == Request::FINISH) // skip if the request is already finished
		return (Logger::log(Logger::DEBUG, "[handleRequest] Request already finished"), -1);

	Logger::log(Logger::DEBUG, "[handleRequest] Handling request from client %d", this->_fd);
	
	char	buffer[CLIENT_READ_BUFFER_SIZE + 1];
	int		bytesRead = 0;

	memset(buffer, 0, CLIENT_READ_BUFFER_SIZE + 1);
	bytesRead  = recv(this->_fd, buffer, CLIENT_READ_BUFFER_SIZE, 0);
	if (bytesRead > 0)
	{
		Logger::log(Logger::DEBUG, "[handleRequest] Received %d bytes from client %d", bytesRead, this->_fd);
		buffer[bytesRead] = '\0';
	}
	 else if (bytesRead < 0)
	{
		this->_request->setError(500);
		return (Logger::log(Logger::ERROR, "[handleRequest] Error with recv function"), -1);
	}
	else if (bytesRead == 0)
		return (0);
	this->_request->parse(buffer);

	if (this->_request->getState() == Request::FINISH)
		modifySocketEpoll(epollFD, this->_fd, RESPONSE_FLAGS);
		// this->handleResponse(epollFD);

	return (bytesRead);
}

bool Client::isCorrectCGIPath(std::string path){
	std::map<std::string, std::string>::const_iterator it;

	for (it = this->_request->getLocation()->getCGI().begin(); it != this->_request->getLocation()->getCGI().end(); ++it)
	{
		if (path.size() > it->first.size() && path.compare(path.size() - it->first.size(), it->first.size(), it->first) == 0){
			if (!fileExist(it->second)){
				Logger::log(Logger::ERROR, "CGI file not found: %s", it->second.c_str());
				// TODO verifer si c'est une 404 ??
				this->_request->setStateCode(404);
				return false;
			}
			if (!fileExist(path)){
				Logger::log(Logger::ERROR, "CGI executable not found: %s", path.c_str());
				this->_request->setStateCode(403);
				return false;
			}
			// TODO set cgi path et cgi extension dans la class CGIHandler
			return true;
		}
	}
	return false;
}

/**
 * @brief fontion qui determine si l'uri demande est un CGI a executer
 */
bool Client::isCGI(){

	if (this->_request->getLocation() == NULL)
		return false;

	std::vector<std::string> allPathsLocations = this->_response->getAllPathsLocation();
	
	for (size_t i = 0; i < allPathsLocations.size(); i++){
		if (isCorrectCGIPath(allPathsLocations[i]))
			return true;
	}

	return false;
}

/**
 * @brief Handle the response of the client
 */
int Client::handleResponse(int epollFD)
{
	if (isCGI()){
		Logger::log(Logger::DEBUG, "[handleResponse] CGI detected");
		if (this->_response->handleCGIResponse(epollFD) != -1)
			return (0);
		//TODO appeler la classe CGIHandler
	}

	// Check if CGI
	
	// Otherwise send classic response
	std::string response = this->_response->getRawResponse();
	Logger::log(Logger::INFO, "Response to sent: \n%s", response.c_str());
	
	int bytesSent = -1;
	if (this->getFd() != -1)
		bytesSent = send(this->getFd(), response.c_str(), response.length(), 0);
	
	if (bytesSent < 0)
		Logger::log(Logger::ERROR, "Error with send function");
	else
		Logger::log(Logger::DEBUG, "Sent %d bytes to client %d", bytesSent, this->getFd());

	if (this->getResponse()->getState() == Response::FINISH)
	{
		Logger::log(Logger::DEBUG, "Response sent to client %d", this->getFd());
		this->reset();
		modifySocketEpoll(epollFD, this->getFd(), REQUEST_FLAGS);
	}
	return (0);
}

/**
* Une fois qu'on a envoye la reponse, il faut clear la requete, a voir comment faire ca clean ?
 */
void Client::reset(void)
{
	// Reset request
	delete this->_request;
	this->_request = new Request(this);
	// Reset response
	delete this->_response;
	this->_response = new Response(this);
}
